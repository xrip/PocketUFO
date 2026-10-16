/* pufo_plane.h - the craft loadout screen's weapon selection.
 *
 * Evidence: BasePlaneScreen_SelectWeapon25..30 @ 0x79b20/0x79b64/0x79ba8/0x79bec/0x79c30/0x79c74
 * (all six on one line: the parity tool reads a citation run on a single line).
 *
 * The loadout screen carries TWO weapon panels. Every one of the six handlers is the same
 * thirteen instructions:
 *
 *     if (bPanelAOpen) nPanelASel = nWeapon;   // unk_161F18 gates unk_161F10
 *     if (bPanelBOpen) nPanelBSel = nWeapon;   // unk_161F1C gates unk_161F14
 *     B BasePlaneScreen_ApplyWeaponSwap        // 0x79748, a true tail call
 *
 * The widths matter and only the disassembly shows them: the gates are read with LDRB and
 * the selection is written with STRBNE, so the selected weapon index is a BYTE, not a dword.
 * The six handlers differ only by that constant, 25..30, so the port keeps one function and
 * takes the index as an argument.
 *
 * A closed panel keeps its old selection, and the swap runs whether or not anything was
 * written. BasePlaneScreen_ApplyWeaponSwap (0x79748) is not ported, so it is a hook, and its
 * result is what these handlers return. It is named without a citation on purpose: a citation
 * credits a function as recreated. */
#ifndef PUFO_PLANE_H
#define PUFO_PLANE_H

#include "pufo_common.h"
#include "game/pufo_ship.h"   /* Ship, BaseRec */
#include "game/pufo_boot.h"   /* ObjectRecFull */
#include "game/pufo_strateg.h" /* StrategItem, StrategList_AddItem */
#include "util/pufo_ptrarray.h" /* PtrArray */
#include "render/pufo_render.h" /* GfxColorEnv, SoftSurf_BlitRectProxy */
#include "ui/pufo_panel.h"   /* UiPanel: nUnk3032 @ 0xBD8, anLineAux @ 0x13B4 */
#include "ui/pufo_control.h" /* UiControl + UiControlDesc, the list nav pair */
#include "game/pufo_baseui.h" /* ResearchTopicLive + g_aBuySellRowOrder, the stores builder's gate and sort */

typedef struct PlaneWeaponPanels {
    u8 nPanelASel;  /* unk_161F10, the weapon the first panel has selected */
    u8 nPanelBSel;  /* unk_161F14, the weapon the second panel has selected */
    u8 bPanelAOpen; /* unk_161F18, gate for the first panel */
    u8 bPanelBOpen; /* unk_161F1C, gate for the second panel */
} PlaneWeaponPanels;

i32 BasePlaneScreen_SelectWeapon(PlaneWeaponPanels *pPanels, i32 nWeaponIndex,
                                 i32 (*pfnApplySwap)(void *pUser), void *pUser);

/* ------------------------------------------------- moving weapon ammo to and from base stores
 *
 * Evidence: BasePlane_UnloadWeaponStores @ 0x790e8.
 *
 * Its argument is a WEAPON INDEX, not a pointer. The decompiler types it BaseRec* but every
 * test in the function is on the low byte, and the callers pass an index straight out of a
 * Ship.bWeaponL or Ship.bWeaponR.
 *
 * Five weapons are handled, each with an object record and a load size:
 *
 *     weapon   object      load
 *     25       apObjects[52]    10
 *     26       apObjects[53]     3
 *     27       apObjects[54]     2
 *     29       apObjects[105]   10
 *     30       apObjects[55]     2
 *
 * Read the base's stock for that object at auPerBaseVal[nBaseIndex], take
 * min(stock, load), add it into the craft's ammo byte for the weapon, and subtract the same
 * amount from the base. That is the whole rule, and it collapses the five branches the
 * original writes out.
 *
 * Weapon 29 is the one that breaks the pattern in appearance only: its carried count is in
 * rounds of 10 per base item, so it adds 10 times the amount taken. Its Load counterpart
 * divides by 10 on the way back.
 *
 * A weapon index outside those five does nothing and changes nothing.
 *
 * Deviation: the original returns whatever BaseRec_SubItemQty returned, which is a BaseRec
 * pointer, and returns the weapon index unchanged for an unhandled weapon. No caller reads
 * either -- BasePlaneScreen_ApplyWeaponSwap discards both. The port returns the amount taken
 * from base stores, which is the only thing worth reporting. */
typedef struct PlaneWeaponEnv {
    Ship *const *apCraft;
    ObjectRecFull *const *apObjects;
    BaseRec *const *apBases;
    i32 nCraftIdx;
    i32 nBaseIdx;   /* the SIGNED byte at gUiRootPanel.anLineAux[79] */
    i32 nObjectCount; /* Map_nObjectCount @ 0x114A60, the walk BasePlane_RearmFromStores needs */
} PlaneWeaponEnv;

i32 BasePlane_UnloadWeaponStores(i32 nWeapon, const PlaneWeaponEnv *pEnv);

/* BasePlane_LoadWeaponStores @ 0x793e0. The reverse: put carried ammo back into base stores.
 *
 * A mode decides how much comes back, computed from the two panel gates before the weapon is
 * looked at. Panel A's gate gives mode 2 unless this weapon is the craft's RIGHT-hand weapon,
 * where it drops to 1. Panel B's gate gives mode 2 unless this weapon is the LEFT-hand weapon,
 * where it drops to 1 -- and that second test OVERWRITES the first, so with both gates set only
 * the second one matters. With neither gate the mode stays 0.
 *
 * Mode 2 means the weapon is replaced outright: the craft keeps one load and only the excess
 * above it goes back to the base. Any other mode returns the entire carried amount and zeroes
 * the byte. Weapon 29 converts between its rounds and base items on both paths.
 *
 * Deviation: the original returns the weapon index on some paths and the selected craft index
 * on the others, which is why no caller may read it as a status. None of them do. The port
 * returns the number of base items added. */
i32 BasePlane_LoadWeaponStores(i32 nWeapon, const PlaneWeaponEnv *pEnv,
                               const PlaneWeaponPanels *pPanels);

/* BasePlaneScreen_ApplyWeaponSwap @ 0x79748. Applies the loadout the two panels selected. It
 * takes no arguments in the original; every input is a global, and this port passes them in.
 *
 * The left and right halves are the same code with the weapon byte and the selection global
 * swapped, so the port keeps one helper and calls it twice.
 *
 * When the craft already carries a weapon the order is: one of the OLD weapon goes back into
 * base stores, the OLD weapon's carried ammo returns, one of the NEW weapon is taken OUT of base
 * stores, the NEW weapon is added to the craft's cargo, the NEW weapon's ammo is loaded from
 * base stores, and only then is one of the OLD weapon removed from the cargo.
 *
 * A weapon and its ammo are SEPARATE object records. The swap moves apObjects[weapon] through
 * cargo and base stores, while the two store functions move apObjects[52..55] and [105]. That is
 * why the take-out running before the load has no effect on the ammo: they touch different
 * records. The order that IS load-bearing is the assignment of the weapon byte, which happens
 * LAST. The cargo removal at 0x79838 re-reads the weapon byte, so assigning it any earlier would
 * remove the NEW weapon from the craft instead of the old one.
 *
 * When the craft carries nothing, the two base-store calls for the old weapon and the cargo
 * removal are absent, and the base-store call for the NEW weapon moves to the END, after the
 * load rather than before it.
 *
 * Both gate flags are cleared at the end whether or not either side did anything, and the
 * original always returns 0. */
i32 BasePlaneScreen_ApplyWeaponSwap(PlaneWeaponPanels *pPanels, const PlaneWeaponEnv *pEnv);

/* ------------------------------------------------- adding an intercept mission
 *
 * Evidence: BasePlaneScreen_AddInterceptMission @ 0x7de14.
 *
 * IDA gives this function no arguments, and that is wrong. The disassembly opens with
 * `MOV R3, R1, LSL #24` then `MOV R3, R3, ASR #24`, which is a sign-extend of R1's low byte,
 * and R1 is then used twice: as the index into the mission-site table and as the bCraftClass
 * argument to StrategList_AddItem. So it takes one signed-char argument.
 *
 * The coordinates come from a table of 24-byte records at 0x1864F8 + 200, that is 0x1865C8,
 * indexed by that craft class. x is the SIGNED 16-bit value at record +0 and y at record +2;
 * the loads are LDRSH, so both are signed. The remaining 20 bytes of each record are not
 * touched here.
 *
 * It then adds a strateg list item with nType 3, bActive 1, bVisible 1 and nId taken from the
 * mission-id counter. Because nType is 3, that id lands in nSiteId and nDisplayId becomes -1.
 * After the add it re-reads the list count, sets the new last item's nTimer to 2880, and stores
 * count-1 into a separate global -- the index of the item it just created. The count is read a
 * third time for that store rather than reused.
 *
 * Finally it post-increments the mission-id counter and returns the incremented value, so the
 * id the item was given is the value the counter held before this call.
 *
 * Deviation: the original indexes the table with the sign-extended char and never bounds it, so
 * a negative craft class reads before the table. The port refuses and adds nothing. */
typedef struct InterceptMissionCtx {
    PtrArray *pList;                  /* the strateg list, 0x186534 */
    i32 *pnZoomMode;                  /* the viewport's nMode, read live by AddItem */
    i32 *pnSeed;                      /* the per-item seed counter, 0x18640C */
    const MissionSiteRecord *apSites; /* the table at 0x1865C8 */
    i32 nSiteCount;
    i32 *pnMissionId;                 /* unk_18654C */
    i32 *pnLastIndex;                 /* 0x186BE4, set to the index of the new item */
} InterceptMissionCtx;

i32 BasePlaneScreen_AddInterceptMission(i32 bCraftClass, const InterceptMissionCtx *pCtx);

/* ------------------------------------------------- the loadout screen's two stat panels
 *
 * Evidence: BasePlaneScreen_DrawStatLabelA @ 0x79e00 and BasePlaneScreen_DrawStatLabelB @ 0x79fc4
 * (452 B each). The two bodies are the same eleven operations and differ in exactly one store: A
 * opens the first panel's gate and B opens the second's. They are the reason BasePlane_RearmFromStores
 * and BasePlane_LoadWeaponStores have gates to consult — drawing a panel is what marks it open.
 *
 * The frame is three nested rectangles through SoftSurf_BlitRectProxy: 5,75 230 wide and
 * 20 * (rowCount + 1) tall in index 0x44 with colours 0xF4/0xFC, then 6,76 228 wide and
 * 20 * rowCount + 18, then 7,77 226 wide and 20 * rowCount + 16, both inner ones index 0x14 with
 * 0x50/0x63. The row count is nPlaneStatRowCount @ 0x161A38, the same global the start-menu flicker
 * reuses as scratch at 0xA0AEC; the port names it g_nPlaneStatRowCount in the app shell.
 *
 * Then the cyan pair (68,244,252)/(4,70,75) and three centred column headers at y 75, centred on
 * 70, 164 and 210. The headers are text records 141, 142 and 143, and the original SHORTENS two of
 * them by writing a NUL straight into the loaded table: byte 7 of record 142 (the shipped record is
 * 10 characters, so it becomes 7) and byte 5 of record 143 (8 characters, becomes 5). The store
 * happens before that header is measured, so it applies to the header drawn next, and it is
 * idempotent — the second visit truncates an already-truncated record. The port writes through the
 * caller's table for the same reason the original does: the shortening IS the label.
 *
 * Deviation: the original returns the last Text_DrawString's value; nothing reads it. The port
 * returns nothing. */
#define PUFO_PLANE_LABEL_REC_A  141
#define PUFO_PLANE_LABEL_REC_B  142
#define PUFO_PLANE_LABEL_REC_C  143
#define PUFO_PLANE_LABEL_B_KEEP 7    /* 0x79F34: record 142 is cut to 7 characters */
#define PUFO_PLANE_LABEL_C_KEEP 5    /* 0x79F78: record 143 is cut to 5 characters */

void BasePlaneScreen_DrawStatLabel(PlaneWeaponPanels *pPanels, i32 nPanel, u8 *pTextRecs,
                                   const GfxColorEnv *pColor, i32 nRowCount);

/* ------------------------------------------------- putting carried weapon ammo back
 *
 * Evidence: BasePlane_RearmFromStores @ 0x78ed0 (536 B).
 *
 * Two halves, one per panel gate, and they are the same loop with the weapon byte swapped: walk
 * every object record 0..nObjectCount-1, and for the one whose INDEX equals the craft's
 * Ship.bWeaponL (left half) or Ship.bWeaponR (right half), and only if the craft actually carries
 * that object (ObjectRecFull.aItemQty[craft] non-zero), give one back to the base, take one out of
 * the craft, run BasePlane_LoadWeaponStores for that weapon, and clear the weapon byte to 0.
 *
 * The object walk is the odd part: the weapon byte is already an index into apObjects, so the loop
 * exists only to prove the craft carries it. The port keeps the walk because the carry test is the
 * function's whole gate, and it is what makes a craft carrying nothing change nothing.
 *
 * Both gates are cleared at the end whether or not either half did anything, and the original
 * returns Map_nObjectCount read at entry; nothing reads it. */
i32 BasePlane_RearmFromStores(PlaneWeaponEnv *pEnv, PlaneWeaponPanels *pPanels);

/* ------------------------------------------------- closing the loadout screen
 *
 * Evidence: BasePlaneScreen_Close @ 0x7c9e8 (480 B).
 *
 * If the screen committed stores, every committed row is handed back: the base gets the items and
 * the craft loses the cargo. Four rows, walked as a 31..35 loop whose 33 arm is empty:
 *
 *     slot  object  cargo column  base delta   cargo delta
 *     31    56      +0x24         30 * count   30 * count
 *     32    57      +0x28          8 * count    8 * count
 *     34    105     +0x2C          5 * count   50 * count
 *     35    58      +0x30          8 * count    8 * count
 *
 * Slot 34 is not a mismatch: object 105 is weapon 29's ammo, which the game counts in rounds of ten
 * per base item, and BasePlaneScreen_CommitStores @ 0x7ac38 moves it the same 5-for-50 on the way
 * in. The other three rows are one-for-one.
 *
 * Then the crew-quota counter (unk_161EE0, the one CommitStores increments per committed row) is
 * cleared and Geoscape_CloseUI runs over the 0x1864F8 control block.
 *
 * Deviation: the original dereferences apCraft[craft] and apBases[base] with no test; the port skips
 * a row whose craft or object is missing. Geoscape_CloseUI (0x8b94c) is not ported and enters as a
 * hook. */
typedef struct PlaneCloseEnv {
    Ship *const        *apCraft;
    ObjectRecFull *const *apObjects;
    BaseRec *const     *apBases;
    i32   nCraftIdx;      /* unk_161AC4, the craft the screen is editing */
    i8    nBaseIdx;       /* nSelectedBaseIdx @ 0x159D81, the signed byte */
    u8    bStoresCommitted; /* byte_161F28: the screen committed at least one row */
    const i32 *anSlotCount; /* unk_161AD0, indexed 31..35: how many of each row were committed */
    i32 *pnCrewQuota;       /* unk_161EE0 */
    void (*pfnCloseUi)(void *pUser); /* Geoscape_CloseUI (0x8b94c), not ported */
    void *pUser;
} PlaneCloseEnv;

void BasePlaneScreen_Close(const PlaneCloseEnv *pEnv);

/* ------------------------------------------------- the hangar stores list: take a row, put one back
 *
 * Evidence: BasePlaneScreen_CommitStores @ 0x7ac38 (948 B) and BasePlaneScreen_AdjustSelectedQty
 * @ 0x7a9b4 (644 B).
 *
 * The stores list is drawn by a panel, and these two handlers work entirely through panels reached
 * from .bss POINTER slots. Each is a UiPanel (7092 B, nUnk3032 @ +0xBD8, anLineAux @ +0x13B4), and
 * the ARM loads the slot then dereferences it - `LDR R0, =unk_161F00; LDR R0, [R0]; LDR R3, [R0,#0xBD8]`
 * - so the model is a pointer per slot, not an inline object:
 *
 *     0x161F00  the panel AdjustSelectedQty reads the selected ROW from (nUnk3032)
 *     0x161EEC  the panel CommitStores reads the selected ROW from (nUnk3032)
 *     0x161AC8  anLineAux[row] = the row's SLOT id (31..35 for the four weapon rows)
 *     0x161A40  anLineAux[row] = how many of that row are still available
 *     0x161EF4  anLineAux[row] = how many of that row have been taken
 *
 * CommitStores: bail when the available count is not positive. Then TWO gates.
 *
 * A row whose slot is OUTSIDE 31..35 is a weapon row and is gated on hold space:
 * BasePlane_CalcWeaponStoresUsed + apObjects[slot].bCargoSize + Base_CalcWeaponStorageUsed must fit
 * inside Craft_GetTypeCargoCap, else 1 goes to 0x1870C8 (a dword store into a byte global), 0 goes to
 * dword_1870B4, and nothing is taken.
 *
 * A row whose slot IS 31..35 is a crew row, and Craft_GetTypeCargoCap is used as a CRAFT CLASS
 * selector rather than as a capacity: cap 0 and cap 120 refuse outright, cap 140 needs the crew quota
 * (pre-incremented) at most 3 with soldiersAboard + 4*quota at most 14, cap 260 needs at most 4 and at
 * most 26, and an unknown craft type skips the crew gate entirely. The quota is incremented BEFORE the
 * gate and put back on either refusal. Then Base_CraftWeaponItemId must cover 30 / 8 / 5 / 8 for slots
 * 31 / 32 / 34 / 35, else 1 goes to 0x1870C8 and 2 to dword_1870B4. Slot 33 has no object record and
 * passes the stock gate without moving anything. On acceptance the base loses the items, the craft
 * gains 30 / 8 / 50 / 8 cargo units - the same 5-for-50 for object 105 that BasePlaneScreen_Close
 * runs in reverse - and the row's taken count goes up, its available count down, and
 * anSlotCarryCount[slot] goes up.
 *
 * AdjustSelectedQty is the undo: it needs the taken count positive, lowers it, raises the available
 * count, lowers anSlotCarryCount[slot], and for slots 31..35 lowers the crew quota and hands the row
 * back exactly the way BasePlaneScreen_Close does - EXCEPT that here the cargo debit is clamped at
 * zero (`if (cargo < delta) cargo = 0`), which BasePlaneScreen_Close does not do. That asymmetry is
 * the original's, not a port choice.
 *
 * Deviation: the original returns register residue on every path (a panel pointer, the quota address,
 * dword_1870B4) and no caller reads it; the port returns 1 when the row changed and 0 when it did not.
 * The original also indexes anLineAux with an unbounded row and faults on a NULL craft, base or object;
 * the port bounds the row to the panel's 500 slots and skips a row whose objects are missing. */
typedef struct PlaneStoresEnv {
    UiPanel *pRowPanel;      /* unk_161F00, the panel AdjustSelectedQty reads the selected row from */
    UiPanel *pCommitPanel;   /* unk_161EEC, the panel CommitStores reads the selected row from */
    UiPanel *pSlotPanel;     /* unk_161AC8, anLineAux[row] is the row's slot id */
    UiPanel *pAvailPanel;    /* unk_161A40, anLineAux[row] is how many are still available */
    UiPanel *pTakenPanel;    /* unk_161EF4, anLineAux[row] is how many have been taken */
    i32 *anSlotCarryCount;   /* anSlotCarryCount @ 0x161AD0 */
    i32 *pnCrewQuota;        /* nPlaneCrewQuota @ 0x161EE0 */
    Ship *const *apCraft;
    i32 nCraftIdx;           /* nSelectedCraftIdx @ 0x161AC4, a DWORD */
    BaseRec *const *apBases;
    i8 nBaseIdx;             /* nSelectedBaseIdx @ 0x159D81, the signed byte */
    ObjectRecFull *const *apObjects;
    i32 nObjectCount;        /* Map_nObjectCount @ 0x114A60 */
    Soldier *const *apSoldiers;
    i32 nSoldierCount;
    u8 *pbShortfallFlag;     /* byte_1870C8, written as a dword */
    i32 *pnResidueFlag;      /* dword_1870B4[0] */
} PlaneStoresEnv;

i32 BasePlaneScreen_CommitStores(PlaneStoresEnv *pEnv);
i32 BasePlaneScreen_AdjustSelectedQty(PlaneStoresEnv *pEnv);

/* ------------------------------------------------- the staged stores change, committed
 *
 * Evidence: BuySellScreen_ApplyQtyChanges @ 0x78d90 (232 B).
 *
 * The hangar rows stage their pending change in anSlotCarryCount[slot]: BasePlaneScreen_CommitStores
 * raises it (0x7AFD8), BasePlaneScreen_AdjustSelectedQty lowers it (0x7AA28). This is the commit.
 * Walk every object slot the map knows about and, for each one, move the amount the array holds
 * between the selected base and the selected craft -- the sign says which way:
 *
 *   negative, the craft is giving: BaseRec_AddItemQty(base, item, -delta) at 0x78E00, then
 *     InvSel_SubItemQty(craft, item, -delta) at 0x78E24. The amount is re-read from the array and
 *     negated a SECOND time at 0x78E1C instead of reusing the first negation.
 *   zero or positive, the craft is taking: BaseRec_SubItemQty(base, item, delta) at 0x78E34, then
 *     InvSel_AddItemQty(craft, item, delta) at 0x78E4C.
 *
 * A zero amount takes the positive branch and moves nothing. The loop runs over Map_nObjectCount
 * slots whatever the array's real length is (the global is 256 dwords), and the whole function ends
 * in a tail call to Geoscape_CloseUI (0x8b94c, not ported) -- including when the object count is
 * not positive,
 * where the loop is skipped and only the close runs.
 *
 * Deviation: the base is apBases[nSelectedBaseIdx] with the SIGNED byte and no bound, so a -1 or an
 * out-of-range byte reads outside the eight-slot array, and apCraft[nSelectedCraftIdx] is
 * dereferenced whatever that dword holds. The port resolves both to NULL when out of range; the two
 * InvSel calls still run, because ShipCargoApply @ 0x45E8C already refuses a NULL owner. */
typedef struct BuySellApplyQtyEnv {
    i32 *anSlotCarryCount;             /* anSlotCarryCount @ 0x161AD0, 256 dwords */
    ObjectRecFull *const *apObjects;   /* Map.apObjects @ Map+0x108 */
    i32 nObjectCount;                  /* Map_nObjectCount @ 0x114A60 */
    BaseRec *const *apBases;           /* Map.apBases @ Map+0x1174 */
    i8 nBaseIdx;                       /* nSelectedBaseIdx @ 0x159D81, the signed byte */
    Ship *const *apCraft;              /* Map.apCraft @ Map+0x10AC */
    i32 nCraftIdx;                     /* nSelectedCraftIdx @ 0x161AC4, a DWORD */
    i32 nCraftCount;                   /* Map_nCraftCount @ Map+0x1194 */
    void (*pfnCloseUi)(void *pUser);   /* Geoscape_CloseUI (0x8b94c), not ported */
    void *pUser;
} BuySellApplyQtyEnv;

void BuySellScreen_ApplyQtyChanges(const BuySellApplyQtyEnv *pEnv);

/* ------------------------------------------------- the crew list's click: assign or unassign
 *
 * Evidence: BasePlaneScreen_FillCrewPanel @ 0x7a188 (620 B). It is installed as the crew panel's
 * pfnClick by BasePlaneScreen_BuildListPanels (0x7a3f4, not ported), and it toggles ONE soldier
 * selected craft and unassigned.
 *
 * The row is the crew panel's nUnk3032, and the panel's aux column carries the SOLDIER index
 * (anLineAux[row] -> apSoldiers[...]), filled by BuildListPanels. Two outcomes:
 *
 *   Soldier.bUnk183 == 255 (not aboard anything) -> try to assign. The gate is per craft type and it
 *   charges FOUR crew per weapon slot the craft already carries, where the count comes from
 *   Craft_CountWeaponSlotsCarried over object records 31..35:
 *
 *       type 0  soldiersAboard + 4 * weaponSlots + 1 <= 14
 *       type 1  soldiersAboard + 1                 <= 12
 *       type 2  soldiersAboard + 4 * weaponSlots + 1 <= 26
 *       other   no gate at all
 *
 *   Refusing changes nothing. Accepting paints both panels' row (255,200,20)/(10,10,10), writes the
 *   craft index into bUnk183, and puts Soldier_FormatDisplayName's string into the status column.
 *
 *   Soldier.bUnk183 != 255 -> unassign. Both rows are painted (240,240,240)/(20,20,20), the status
 *   column gets text record 93 ("Персон. броня" - what the soldier becomes once unassigned), and -1
 *   goes into bUnk183. There is no gate on this half.
 *
 * Deviation: the original copies the status string into the 50-byte line buffer with an unbounded
 * loop and indexes apSoldiers without a bound; the port bounds both to the line buffer and the
 * roster. The original's return value is register residue and nothing reads it. */
#define PUFO_PLANE_CREW_UNASSIGNED_REC 93 /* 0x7A364: the status column when a soldier is let go */

typedef struct PlaneCrewPanelEnv {
    UiPanel *pCrewPanel;    /* dword_161EDC, the crew list; this function is its pfnClick */
    UiPanel *pStatusPanel;  /* dword_161A3C, the right-hand column drawn beside it */
    Ship *const *apCraft;
    i32 nCraftIdx;          /* nSelectedCraftIdx @ 0x161AC4 */
    Soldier *const *apSoldiers;
    i32 nSoldierCount;
    ObjectRecFull *const *apObjects;
    i32 nObjectCount;       /* Map_nObjectCount @ 0x114A60 */
    const u8 *pTextRecs;    /* aTextRecs @ 0xB38D0 */
    char *pszNameBuf;       /* the original's shared scratch at 0x186B10 (szRadarMsg, 108 B) */
    i32 cchNameBuf;
} PlaneCrewPanelEnv;

void BasePlaneScreen_FillCrewPanel(PlaneCrewPanelEnv *pEnv);

/* ------------------------------------------------- building the crew list's two panels
 *
 * Evidence: BasePlaneScreen_BuildListPanels @ 0x7a3f4 (1472 B).
 *
 * One flag decides the whole function: unk_161F24. Set, the function is a teardown -- free the two
 * controls' pads and the controls themselves, then the two panels' line buffers and the panels.
 * Clear, it builds them and fills the rows, and sets the flag.
 *
 * Building: two UiControls from the descriptor pair at 0xABC48 (g_aPlaneListNav: 205,20 "up"/"sup" and
 * 205,40 "down"/"sdown"), installed with the BuySellScreen_ListPanelPrev/Next pair @ 0x79a20 / 0x79a48,
 * which page the SAME two panels. Then two UiPanels of 0x1BB4 bytes each with nLineCount =
 * unk_161AB4 + unk_161AB8: the crew list at x 10 (view width 230) and its status column at x 190 (view
 * width 30), both y 70, nStyle 14, nFont 16, colours B4/B4/B4 then F0,F0,F0/14,14,14 and 32,32,250. The
 * status panel additionally clears bFlag7073 and sets nUnk7076 to 1; the crew panel gets
 * pfnClick = BasePlaneScreen_FillCrewPanel.
 *
 * Filling walks every soldier and keeps the ones whose Soldier.nKind equals the selected base AND whose
 * craft byte is either 255 (unassigned) or the selected craft. The crew column gets Soldier.szName, the
 * status column gets Soldier_FormatDisplayName's craft label, and the panel's aux column gets the
 * soldier index -- which is what BasePlaneScreen_FillCrewPanel later clicks. A row already aboard the
 * selected craft is painted (255,200,20)/(10,10,10); a row with the 254 sentinel (aboard a craft that is
 * out) is painted (255,100,100)/(10,10,10).
 *
 * The status column formats soldier `scroll + i` (the panel's nUnk3028 is added at 0x7A7B8) while the
 * crew column shows soldier `i`'s name and the aux column records `i`. That reads like a scrolled-list
 * bug and is not one: the panel is allocated earlier in this same call, so its scroll cursor is always
 * zero when the fill runs, and the addition is dead code as written. The port keeps the addition because
 * that is the code, and bounds it.
 *
 * The 254 sentinel only paints a row when the SELECTED CRAFT index is itself 254: the row filter admits
 * 255 or the selected craft index, so the (255,100,100) arm is reachable only through that pairing.
 *
 * Deviation: the original dereferences a soldier roster slot and a panel line buffer with no test and
 * indexes apSoldiers past its end when the scroll runs off the roster; the port skips a NULL slot,
 * bounds the scroll index, and frees a panel whose line buffers could not be allocated (the original
 * would keep a panel with unallocated lines). */
#define PUFO_PLANE_LIST_NAV 2

typedef struct PlaneListPanels {
    UiControl *apNav[PUFO_PLANE_LIST_NAV]; /* 0x161AA4, 0x161AA8 */
    UiPanel   *pCrewPanel;                 /* 0x161EDC */
    UiPanel   *pCrewStatusPanel;           /* 0x161A3C */
} PlaneListPanels;

typedef struct PlaneListPanelsEnv {
    bool  *pbBuilt;             /* unk_161F24 */
    i32    nLineCount;          /* unk_161AB4 + unk_161AB8 */
    Ship *const *apCraft;
    Soldier *const *apSoldiers;
    i32    nSoldierCount;
    i8     nBaseIdx;            /* nSelectedBaseIdx @ 0x159D81 */
    i32    nCraftIdx;           /* nSelectedCraftIdx @ 0x161AC4 */
    const u8 *pTextRecs;        /* aTextRecs @ 0xB38D0 */
    char  *pszNameBuf;          /* the shared scratch at 0x186B10 (szRadarMsg, 108 B) */
    i32    cchNameBuf;
    void (*apfnNavClick[PUFO_PLANE_LIST_NAV])(void); /* BuySellScreen_ListPanelPrev / ListPanelNext */
    void (*pfnCrewClick)(void);                      /* BasePlaneScreen_FillCrewPanel */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1, const char *psz2,
                        void *pUser);
    void *pPadUser;
} PlaneListPanelsEnv;

void BasePlaneScreen_BuildListPanels(PlaneListPanels *pOut, const PlaneListPanelsEnv *pEnv);

/* --- BasePlaneScreen_BuildPanels @ 0x7cc8c (1768 B) : the hangar craft list, and the teardown that
 *     frees the whole hangar object block ---
 *
 * One byte decides the direction: bPlanePanelsBuilt @ 0x161F08 (LDRB / STRB, so a byte).
 *
 * BUILT (0x7D234..0x7D368): the flag goes down and everything the hangar owns is released - the two
 * list panels (pPlaneCraftPanel 0x161EFC, pPlaneCraftStatusPanel 0x161ED0, each UiPanel_FreeLines +
 * delete), then the object block BasePlaneScreen_Init (0x7d9e4, not ported) allocated at 0x161A48: the
 * backdrop pad
 * at +0, five craft art pads at +4..+0x14, six weapon icon pads at +0x18..+0x2C, four main controls at
 * +0x30..+0x3C (UiControl_FreePads + delete) and seven weapon controls at +0x40..+0x58. The original
 * frees through the slots without nulling them; the port nulls them, which is the deviation that makes
 * a second teardown harmless.
 *
 * NOT BUILT (0x7CCAC..0x7D22C): the flag goes up, then
 *   1. nRows = how many of apCraft[0..Map_nCraftCount) have Ship.bHomeBase @ +0x03 equal to
 *      nSelectedBaseIdx (0x7CCD8..0x7CCFC; Map_nCraftCount is a byte, read with LDRB).
 *   2. panel A @ 0x161EFC, nRows lines: nStyle 8, nFont 16, pfnClick = BasePlaneScreen_SelectCraft,
 *      nPosX 10, nPosY 110, nUnk7052 5, nUnk7056 110, nViewW 230, gap_0[0..2] B4 B4 B4,
 *      gap_0[6..11] F0 F0 F0 14 14 14, bFlag7073 1, bUnk7088 0x96. nUnk7076 keeps the ctor's 0, so the
 *      names are left-aligned.
 *   3. panel B @ 0x161ED0, nRows lines: the same style, pfnClick = BasePlaneScreen_SelectListed,
 *      nPosX 230, nViewW 30, gap_0[0..2] B4 B4 B4, bFlag7073 1, bUnk7088 0x96, nUnk7076 2 (right
 *      aligned). It gets no SetBytes6to11 call, so gap_0[6..11] stays at the ctor's zero.
 *   4. the row pass (0x7CF30..0x7D22C) walks aPlaneCraftTypeOrder @ 0xABE28 - five dwords, 0,1,2,3,4 -
 *      and for each type walks every craft, so the list is grouped by craft type rather than roster
 *      order. A craft is listed when bHomeBase == nSelectedBaseIdx and bCraftType == the table entry.
 *      Panel A gets Ship_FormatDisplayName's string and anLineAux = the ROSTER index. Panel B's status
 *      column and its row colours come from four Ship fields:
 *        bAtBase @ +0x06 == 0        -> text 152 "Статус",            (150,150,250 / 25,10,10)
 *        bAtBase and bRefueling @ +0x07 -> text 149 "Заправка",       (250, 50, 50 / 25,10,10)
 *        at base, not refuelling, uRangeRemain @ +0x08 != uRangeMax @ +0x0A
 *                                     -> text 150 "Готов",            (100,200,100 / 10,25,10)
 *        at base, not refuelling, uRangeRemain == uRangeMax
 *                                     -> text 151 "На миссии",        (250,250,250 / 25,10,10)
 *      The last two read backwards for a craft that is sitting at base - full range is labelled "on a
 *      mission" and short range "ready" - which is what the ARM does (0x7D088..0x7D174) and what the
 *      port reproduces rather than "corrects".
 *
 * Deviations: the original reaches the object block through the fixed global at 0x161A48 and the two
 * panels through their own globals; the port takes all of them from the env and the out struct. The
 * original copies each name with an unbounded loop into a 50-byte line buffer; the port bounds it. */
#define PUFO_PLANE_STATUS_REFUELING 149 /* aTextRecs: "Заправка", at base and bRefueling */
#define PUFO_PLANE_STATUS_READY     150 /* "Готов", at base, not refuelling, range below max */
#define PUFO_PLANE_STATUS_FULL      151 /* "На миссии", at base, not refuelling, range at max */
#define PUFO_PLANE_STATUS_OUT       152 /* "Статус", not at base */

/* The IDB's own BasePlaneScreen type (size 92), laid over the block at 0x161A48. */
typedef struct BasePlaneScreen {
    PadImage  *pBackdropPad;   /* 0x161A48, the "plane" backdrop */
    PadImage  *apCraftPads[5]; /* 0x161A4C..0x161A5C, the baseb001..005 craft art */
    PadImage  *apWeaponPads[6];/* 0x161A60..0x161A74, the baseb049..054 weapon icons */
    UiControl *apMainCtl[4];   /* 0x161A78..0x161A84 */
    UiControl *apWeaponCtl[7]; /* 0x161A88..0x161AA0 */
} BasePlaneScreen;

typedef struct PlaneCraftPanels {
    UiPanel *pCraftPanel;  /* pPlaneCraftPanel @ 0x161EFC */
    UiPanel *pStatusPanel; /* pPlaneCraftStatusPanel @ 0x161ED0 */
} PlaneCraftPanels;

typedef struct PlaneBuildEnv {
    u8      *pbBuilt;             /* bPlanePanelsBuilt @ 0x161F08 */
    BasePlaneScreen *pObjects;  /* the block at 0x161A48 */
    Ship *const *apCraft;         /* apCraft @ 0x114A64 */
    const u8 *pnCraftCount;       /* Map_nCraftCount @ 0x114B4C, a byte */
    const i8 *pnSelectedBaseIdx;  /* nSelectedBaseIdx @ 0x159D81 */
    const u8 *pTextRecs;          /* aTextRecs @ 0xB38D0 */
    char *pszNameBuf;             /* the shared scratch at 0x186B10 (szRadarMsg, 108 B) */
    i32  cchNameBuf;
    void (*pfnCraftClick)(void);  /* BasePlaneScreen_SelectCraft @ 0x7cbc8, panel A's click */
    void (*pfnStatusClick)(void); /* BasePlaneScreen_SelectListed @ 0x7cc3c, panel B's click */
} PlaneBuildEnv;

void BasePlaneScreen_BuildPanels(PlaneCraftPanels *pOut, const PlaneBuildEnv *pEnv);

/* --- BasePlaneScreen_SelectCraft @ 0x7cbc8 (116 B) and BasePlaneScreen_SelectListed @ 0x7cc3c (80 B)
 *     : the two craft-list row clicks ---
 *
 * Both take `pPlaneCraftPanel->anLineAux[cursor]` - the roster index the row pass stored - into
 * nPlaneSelCraftIdx @ 0x161F04 and nSelectedCraftIdx @ 0x161AC4. They differ in two ways:
 *
 *   - SelectCraft (panel A's click) reads the cursor from panel A itself; SelectListed (panel B's
 *     click) reads it from panel B's nUnk3032 and then indexes panel A's anLineAux with it (0x7CC54..
 *     0x7CC68, confirmed in ARM: LDR R0,[=unk_161ED0]; LDR R3,[R0,#0xBD8]; then unk_161EFC's
 *     anLineAux at +0x13B4). The two panels are built with the same line count and their cursors move
 *     together through UiPanel_ClickRow, so it works - but it is the original's shape, not a
 *     simplification the port is free to make.
 *   - SelectCraft then tests apCraft[index]->bAtBase (LDRB [R3,#Ship.bAtBase] at 0x7CC14 - the IDB
 *     comment said bBusy and that was wrong) and, when the craft is at base, sets
 *     bPlaneShowCraftDetail @ 0x161F20 with a STRB. BasePlaneScreen_Draw (0x7d374, not ported) reads
 *     that byte
 *     first: set, it draws BasePlaneScreen_DrawCraftDetail and returns; clear, it draws the two list
 *     panels. So selecting a craft that is at base switches the hangar to the detail view.
 *     SelectListed touches neither.
 *
 * The original returns the craft index (SelectCraft returns &bPlaneShowCraftDetail on the at-base
 * path); neither return value is used - both are installed as pfnClick, which is void. */
typedef struct PlaneCraftSelectEnv {
    UiPanel *pCraftPanel;  /* pPlaneCraftPanel @ 0x161EFC */
    UiPanel *pStatusPanel; /* pPlaneCraftStatusPanel @ 0x161ED0, SelectListed reads its cursor */
    i32     *pnSelCraftIdx;      /* nPlaneSelCraftIdx @ 0x161F04 */
    i32     *pnSelectedCraftIdx; /* nSelectedCraftIdx @ 0x161AC4 */
    Ship *const *apCraft;        /* apCraft @ 0x114A64 */
    u8      *pbShowCraftDetail;  /* bPlaneShowCraftDetail @ 0x161F20 */
} PlaneCraftSelectEnv;

i32 BasePlaneScreen_SelectCraft(const PlaneCraftSelectEnv *pEnv);
i32 BasePlaneScreen_SelectListed(const PlaneCraftSelectEnv *pEnv);

/* --- BasePlaneScreen_Init @ 0x7d9e4 (1072 B) : allocate and wire the whole hangar object block ---
 *
 * In the original's order:
 *   1. a PadImage (operator new 0x14 + PadImage_ctor) at +0, loaded with the pad "plane" (aPlane_0
 *      @ 0xABEC4) at mode 0, nOriginX / nOriginY 0 (0x7D9F0..0x7DA30).
 *   2. four UiControls (operator new 0x20 + UiControl_ctor) at +0x30..+0x3C. Their clicks are
 *      BasePlaneScreen_DrawStatLabelA (0x79e00), DrawStatLabelB (0x79fc4), BuildListPanels (0x7a3f4)
 *      and BuildControls (0x7b014); then each gets UiControl_Setup(ctl, 0, 0, g_aPlaneMainDesc[i]) over
 *      the four 32-byte records at 0xABCC8 (0x7DA7C..0x7DAC8).
 *   3. seven UiControls at +0x40..+0x58. Their clicks are BasePlane_RearmFromStores (0x78ed0) and
 *      BasePlaneScreen_SelectWeapon25..30 (0x79b20..0x79c74); then UiControl_Setup over the seven
 *      records at 0xABD48 (0x7DB2C..0x7DB9C).
 *   4. five PadImages at +0x04..+0x14, loaded baseb001..baseb005 at mode 3, centred with
 *      nOriginX = 120 - (uWidth >> 1) and nOriginY 80 (0x7DBA4..0x7DCDC).
 *   5. six PadImages at +0x18..+0x2C, loaded baseb049, baseb050, baseb051, baseb053, baseb054,
 *      baseb052 at mode 3 — note the last two are swapped relative to their numbers — and all six get
 *      nOriginY 95 from the offset loop at 0x7DD88..0x7DD9C.
 *   6. unk_186B00 goes to x 224, y 1 with BasePlaneScreen_Close (0x7c9e8) as its click, and unk_186BA4
 *      gets BuySellScreen_ApplyQtyChanges (0x78d90) — the same confirm slot the buy/sell screen uses.
 *   7. it tail calls BasePlaneScreen_BuildPanels.
 *
 * Deviations: the original dereferences every allocation it just made without a null test; the port
 * skips the wiring of a control or pad that failed to allocate. PadImage_ctor (0x11000) only sets
 * pData, so the original's freshly allocated pads carry an uninitialised uWidth into the
 * `120 - (uWidth >> 1)` at 0x7DC60 whenever the load fails; the port zero-fills the pad.
 * BasePlaneScreen_BuildControls and BuySellScreen_ApplyQtyChanges are not ported and enter as hooks. */
#define PUFO_PLANE_MAIN_CTLS 4
#define PUFO_PLANE_WEAPON_CTLS 7
#define PUFO_PLANE_CRAFT_PADS 5
#define PUFO_PLANE_WEAPON_PADS 6

typedef struct BasePlaneScreenInitEnv {
    BasePlaneScreen *pScreen;             /* the block at 0x161A48 */
    UiControl *pCloseCtl;                 /* unk_186B00 */
    UiControl *pBuySellCtl;               /* unk_186BA4 */
    void (*apfnMainClick[PUFO_PLANE_MAIN_CTLS])(void);   /* DrawStatLabel, BuildListPanels, BuildControls */
    void (*apfnWeaponClick[PUFO_PLANE_WEAPON_CTLS])(void); /* RearmFromStores, SelectWeapon25..30 */
    void (*pfnCloseClick)(void);          /* BasePlaneScreen_Close @ 0x7c9e8 */
    void (*pfnBuySellClick)(void);        /* BuySellScreen_ApplyQtyChanges (0x78d90), not ported */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1, const char *psz2,
                        void *pUser);
    void *pPadUser;
    PlaneCraftPanels *pPanels;            /* out: the two craft-list panels BuildPanels allocates */
    const PlaneBuildEnv *pBuildEnv;       /* what BuildPanels needs, on the same object block */
} BasePlaneScreenInitEnv;

void BasePlaneScreen_Init(const BasePlaneScreenInitEnv *pEnv);

/* --- BasePlaneScreen_BuildControls @ 0x7b014 (2,788 B) : the hangar's stores-transfer panel set ---
 *
 * One byte decides the direction: bPlaneStoresCommitted @ 0x161F28, read with LDRB at 0x7B02C and written
 * with STRB at 0x7B12C and 0x7BA18. The IDB sized this global 12 bytes; every access to it in the binary
 * is a byte access, so the size is corrected there.
 *
 * COMMITTED (0x7BA10..0x7BAF8): the flag goes down FIRST (0x7BA18), before anything is released. Then the
 * two store-nav controls at the object block's +0x64 / +0x68 (0x161AAC, 0x161AB0 -- `ADD R4,R4,#4` over two
 * steps starting from pPlaneBackdropPad at 0x7BA1C) go through UiControl_FreePads + operator delete, and
 * then the five panels in the order slot (0x7BA48), sel-row (0x7BA68), avail, commit-row, taken. The
 * original frees through the globals and never nulls them.
 *
 * NOT COMMITTED (0x7B038..0x7BA00), in the original's order:
 *   1. anSlotCarryCount[256] is zeroed (0x7B038..0x7B058: `MOV R3,#0x100`, `SUBS R3,#1` / `BPL`, so 256
 *      stores, not 255).
 *   2. nPlaneCrewQuota = what this craft already carries of the five crew slots: apObjects[31..35]
 *      ->aItemQty[nSelectedCraftIdx]. The loop reaches slot 31 as the context base 0x1139B8 + 0x184
 *      (0x7B07C), which is apObjects + 0x7C, and stores the running total on every step (0x7B070), so the
 *      global is written five times rather than once at the end.
 *   3. two UiControls (operator new 0x20 + UiControl_ctor) into 0x161AAC / 0x161AB0 (0x7B0A8..0x7B0E8).
 *   4. their clicks are written by a direct store at +0xC BEFORE UiControl_Setup: BuySellScreen_
 *      DetailPanelPrev on the first (0x7B0F4..0x7B0FC), DetailPanelNext on the second. These are the same
 *      two handlers the buy/sell screen installs -- they page five panels, and here they page the hangar's.
 *   5. UiControl_Setup(ctl, 0, 0, desc) over the two 32-byte descriptors at 0xABC88: {205, 18, "up", "sup",
 *      ""} and {205, 38, "down", "sdown", ""}. The crew-list pair at 0xABC48 carries the same two names
 *      with y 20 and 40, so the two nav pairs differ only in those offsets.
 *   6. bPlaneStoresCommitted goes up (0x7B12C) before a single row exists -- the flag means "this panel set
 *      is live", not "a row was committed", which is why BasePlaneScreen_Close tests it the same way.
 *   7. the row scan (0x7B130..0x7B22C) walks slot 1..nObjectCount-1 and admits a slot only when
 *      `slot < 25 || (slot > 30 && slot < 52)` -- ARM 0x7B190 `CMP #0x19 / BLT`, 0x7B198 `CMP #0x1E / BLE
 *      skip`, 0x7B1A0 `CMP #0x34 / BGE skip` -- and then only when the base stocks it or the craft carries
 *      it (0x7B1B8 `LDRH [obj,#0x2A]` = auPerBaseVal[base], 0x7B1C8 `LDRH [obj,#0x3A]` = aItemQty[craft])
 *      AND its unlocking research topic is unlocked: `aResearchTopics[byte_AA968[slot]].bUnlocked`, ARM
 *      0x7B1D4 `LDRB [slot, table]`, `MUL #0x22` (34 = sizeof ResearchTopicLive), `LDRB [#0xD]`. The pair
 *      (base stock, craft carry) and the row counter are all under the same STRNE (0x7B1F0, 0x7B1FC,
 *      0x7B204), so a slot the topic gate refuses leaves a zero pair behind and the row pass skips it again.
 *   8. five panels, each operator new 0x1BB4 + UiPanel_ctor(nLineCount). All five share nStyle 14, nFont
 *      16, nPosY 70, nUnk7056 70, gap_0[0..2] B4,B4,B4 and gap_0[6..11] F0,F0,F0,14,14,14, and differ as:
 *
 *        panel        slot       nPosX  nUnk7052  nViewW  click                       extras
 *        slot         0x161AC8     10       5        125   Cache_ObjPlus0xBD8 thunk   bFlag7073 1, bUnk7088 0x96
 *        avail        0x161A40    164     130         50   none                       bFlag7073 0, bFlag7074 1, nUnk7076 2
 *        sel-row      0x161F00    170     166         15   AdjustSelectedQty          bFlag7073 1, gap_0[3..5] 32,32,FA, nUnk7084 5, bUnk7088 0x96
 *        commit-row   0x161EEC    186     182         15   CommitStores               same as sel-row
 *        taken        0x161EF4    232     190         45   none                       bFlag7073 0, bFlag7074 1, nUnk7076 2
 *
 *      nUnk7084 5 on the two 15-wide row panels is the auto-repeat count UiPanel_ClickRow consults, so
 *      holding the "<" or ">" row repeats the quantity change every fifth click.
 *   9. the row pass (0x7B7D0..0x7B930) walks the same slot range, re-tests the stored pair, and for a kept
 *      slot copies apObjects[slot]->szName through a 112-byte stack buffer cut to 17 characters when strlen
 *      is above 0x11 (0x7B838) into the slot panel's line; that panel's aux column gets the SLOT id; the
 *      sel-row line is "<" (aRowQtyDec @ 0xABC24) and the commit-row line is ">" (aRowQtyInc @ 0xABC20);
 *      and the two counts go into the avail and taken panels' aux columns -- the decompiler writes them as
 *      `gap_0[row*4+5044]`, and 5044 is anLineAux at +0x13B4. The row index advances only for kept slots
 *      while the slot counter advances for all of them, which is what makes the aux column a slot id rather
 *      than a row index.
 *  10. the sort (0x7B950..0x7B9F8) reads the first nObjectCount-1 entries of g_aBuySellRowOrder @ 0xAAA04
 *      and, for each, swaps the row whose slot-panel aux equals it with the next placed row -- in the slot,
 *      avail and taken panels only. The buy/sell builders swap five panels at this point; this one swaps
 *      three, and it bounds the table walk by the object count rather than by the table's 148 entries.
 *
 * Deviations: the original dereferences each freshly allocated panel straight after the ctor (0x7B218 writes
 * `v22->nStyle` with no test) and writes the two control clicks through the globals with no test either; the
 * port skips a failed allocation. The scan buffer is the 2032-byte stack block at 0x7B018, 254 pairs, and
 * the scan writes one pair per slot with no bound, so a Map_nObjectCount above 255 would run past it; the
 * shipped object count is 149, and the port bounds the scan at 254 pairs anyway. The original also indexes
 * auPerBaseVal with the unbounded signed base byte and aItemQty with the unbounded craft index; the port
 * resolves both to zero when out of range. */
#define PUFO_PLANE_STORE_NAV      2   /* the two nav controls at the object block's +0x64 / +0x68 */
#define PUFO_PLANE_CARRY_SLOTS  256   /* anSlotCarryCount @ 0x161AD0, the count the build zeroes */
#define PUFO_PLANE_SCAN_SLOTS   254   /* the 2032-byte scan block at 0x7B018: one (stock, carry) pair each */
#define PUFO_PLANE_ROW_DEC        "<" /* aRowQtyDec @ 0xABC24: the sel-row column */
#define PUFO_PLANE_ROW_INC        ">" /* aRowQtyInc @ 0xABC20: the commit-row column */
#define PUFO_PLANE_CREW_SLOT_FIRST 31 /* the five crew slots the crew quota sums */
#define PUFO_PLANE_CREW_SLOT_LAST  35
#define PUFO_PLANE_SLOT_TOPIC_COUNT 156 /* byte_AA968 @ 0xAA968, up to the next global at 0xAAA04 */
#define PUFO_PLANE_CRAFT_QTY_SLOTS 52   /* ObjectRecFull.aItemQty @ +0x3A, the same 52 the filter's
                                         * `slot < 52` stops at */

typedef struct PlaneStorePanels {
    UiControl *apNavCtl[PUFO_PLANE_STORE_NAV]; /* 0x161AAC, 0x161AB0 */
    UiPanel *pSlotPanel;   /* pPlaneSlotPanel @ 0x161AC8: the item name column */
    UiPanel *pAvailPanel;  /* pPlaneAvailPanel @ 0x161A40: aux = what the base still has */
    UiPanel *pSelRowPanel; /* pPlaneSelRowPanel @ 0x161F00: the "<" column */
    UiPanel *pCommitPanel; /* pPlaneCommitRowPanel @ 0x161EEC: the ">" column */
    UiPanel *pTakenPanel;  /* pPlaneTakenPanel @ 0x161EF4: aux = what the craft carries */
} PlaneStorePanels;

typedef struct PlaneStoreBuildEnv {
    u8   *pbStoresCommitted;          /* bPlaneStoresCommitted @ 0x161F28, a byte */
    i32  *anSlotCarryCount;           /* anSlotCarryCount @ 0x161AD0 */
    i32  *pnCrewQuota;                /* nPlaneCrewQuota @ 0x161EE0 */
    ObjectRecFull *const *apObjects;  /* apObjects @ 0x113AC0, the context's +0x108 */
    i32   nObjectCount;               /* Map_nObjectCount @ 0x114A60, read with LDRH */
    i32   nCraftIdx;                  /* nSelectedCraftIdx @ 0x161AC4, a dword */
    i8    nBaseIdx;                   /* nSelectedBaseIdx @ 0x159D81, the signed byte */
    const ResearchTopicLive *aTopics; /* aResearchTopics @ 0x15EF08 */
    const u8 *abSlotTopic;            /* byte_AA968 @ 0xAA968: the topic that unlocks each slot */
    void (*apfnNavClick[PUFO_PLANE_STORE_NAV])(void); /* BuySellScreen_DetailPanelPrev / Next */
    void (*pfnSlotClick)(void);       /* Cache_ObjPlus0xBD8_To_dword_186BE8 @ 0x7afec */
    void (*pfnSelRowClick)(void);     /* BasePlaneScreen_AdjustSelectedQty @ 0x7a9b4 */
    void (*pfnCommitClick)(void);     /* BasePlaneScreen_CommitStores @ 0x7ac38 */
    bool (*pfnLoadPads)(UiControl *pCtl, const char *psz0, const char *psz1, const char *psz2,
                        void *pUser);
    void *pPadUser;
} PlaneStoreBuildEnv;

void BasePlaneScreen_BuildControls(PlaneStorePanels *pOut, const PlaneStoreBuildEnv *pEnv);

/* --- BasePlaneScreen_Draw @ 0x7d374 (1,648 B) : the hangar frame, the craft readout, the rails ---
 *
 * Three gates, and they do not gate the same amount of the screen:
 *
 *   1. bPlaneShowCraftDetail @ 0x161F20 (0x7D37C LDRB, `ANDS R3,R0,#0xFF`): set, the function calls
 *      BasePlaneScreen_DrawCraftDetail and returns its value straight from 0x7D394 -- nothing
 *      else here runs. SelectCraft raises that byte when the picked craft is at base, so the detail view
 *      owns the screen. It is not ported yet and enters as a hook.
 *   2. bPlanePanelsBuilt @ 0x161F08 (0x7D398): clear, the two list panels and the whole craft readout are
 *      skipped -- but the two captions and all four rail loops still run, because this `BEQ` and the
 *      preview gate's `BEQ` both land on loc_7D640, the second colour pair, and the rails follow it.
 *   3. bBaseFacPreviewActive @ 0x18652C (0x7D3D0) AND nSmapCursorY @ 0x186BD4 inside the status panel's row
 *      band: `nPosY < cursor < nPosY + 16 * nLineCount` (0x7D3E0 reads nPosY at +0x1B88 through
 *      `MOV R3,#0x1B80 / ORR R3,#8`, 0x7D3FC reads nLineCount at +0xBDC, 0x7D408 `ADD R3,R2,R3,LSL#4`;
 *      `BLE` and `BGE` both skip). nSmapCursorY is read with LDRSH and written with STRH (Geoscape_
 *      OnPointer 0x872F8), so it is the signed 16-bit pointer row the pointer dispatcher keeps live for
 *      whichever screen owns the pointer -- the hangar's included. Unlike the buy/sell, manuf and research
 *      frames, this band has no nStyle cap.
 *
 * Panels (0x7D3B4, 0x7D3C4): pPlaneCraftPanel @ 0x161EFC first, then pPlaneCraftStatusPanel @ 0x161ED0,
 * both through UiPanelMsg_Draw.
 *
 * The readout (0x7D41C..0x7D63C). All three sums index apObjects[i]->aItemQty with
 * nPlaneSelCraftIdx @ 0x161F04 -- the draw uses THAT global, not the nSelectedCraftIdx @ 0x161AC4 the
 * stores builder uses -- and each walk runs 0..Map_nObjectCount-1 with the count read LDRH at 0x7D41C,
 * skipping a slot whose aItemQty is zero before the band test:
 *   - v3 = slots 25..30, the six weapon slots the hangar's own weapon columns cover (0x7D45C `CMP #0x18 /
 *     BLE`, 0x7D464 `CMP #0x1F / ADDLT`). Drawn against TEXTS[146] "Вооружение" as "<v3>\\<bCrewSlots>":
 *     itoa(v3), then strcat with the single backslash at asc_AB9F0 @ 0xAB9F0, then strcat with
 *     itoa(apCraft[nPlaneSelCraftIdx]->bCrewSlots @ +0x14) (0x7D588..0x7D5E0).
 *   - v4 = slots 31..35, the same five crew slots the crew quota sums (0x7D4A4 `CMP #0x1E / BLE`, 0x7D4AC
 *     `CMP #0x24 / ADDLT`), drawn against TEXTS[148] "ТОП кол-во".
 *   - v5 = the soldiers whose Soldier.bUnk183 @ +0xB7 equals the craft index (0x7D4C4..0x7D500:
 *     nSoldierCount read LDRH, the pointer array at the context's +0x1198), drawn against TEXTS[147]
 *     "Экипаж". The compare is `CMP R3,R5` with R3 the zero-extended byte and R5 the dword index, so a
 *     craft index above 255 matches nothing.
 *
 * Two colour pairs, five captions and three values. The captions are TEXTS records at aTextRecs @ 0xB38D0;
 * the IDB's own TEXTS region is zero-filled because the text loads from lang.dat at runtime, so the five
 * strings were read out of lang.dat at 0x102 + 200*n:
 *   Gfx_PackRgb565Pair(250,250,250 / 20,25,20) (0x7D504), then TEXTS[146] at (25,23), TEXTS[147] at (25,43),
 *   TEXTS[148] at (25,63); the values at (190,23), (195,43), (195,63).
 *   Gfx_PackRgb565Pair(250,250,250 / 10,10,25) (0x7D640), then TEXTS[124] "Корабль: " at (25,90) and
 *   TEXTS[153] "Статус" at (165,90).
 *
 * Four rail loops of SoftSurf_BlitRectProxy, every index triple stored with STRB:
 *   A/B (0x7D6AC..0x7D840) are the shared screen rails -- 24 steps of 7 on the sides at x 5 and 234, 24
 *     steps of 5 on the top and bottom at y 5 and 315, the same 5*(k+20) / 230-5k / 250-5k ramps the
 *     transfer, manuf and research frames carry, so the port calls BaseUi_DrawFrameRails.
 *   C (0x7D840..0x7D8FC) is the inner frame's top and bottom edges: a climbing x from 15 and a descending x
 *     from 220, both stepping 5 while the descending one stays above 115 (`CMP #0x73 / BGT`), 21 steps, 5x1
 *     rects at y 15 and 85, colour 5*(n+20) with n 0..20.
 *   D (0x7D8FC..0x7D9B4) is the inner frame's left and right edges: x 15 and 224, 1x2 rects at y 2*v+15 and
 *     m+83 with m from 0 down in steps of 2 (`MOV R3,#0xFFFFFFDC / CMP R7,R3 / BGT`). Both inner loops test
 *     AFTER the step, so the last body is the one that lands on the bound: rail C's is x2 = 120 (21 bodies,
 *     n 0..20, colour 100..200) and rail D's is m = -34 (18 bodies, v 0..17, colour 100..185). Rail D's two
 *     halves therefore meet at y 49..50 rather than leaving a gap. C and D together close a rectangle from
 *     (15,15) to (224,85) around the readout -- the two x runs 15..119 and 120..224 are continuous -- and
 *     the rails run AFTER the text, so they paint over it where they overlap.
 *
 * Deviations: the original dereferences every apObjects and apSoldiers entry it walks, both panel pointers
 * and the craft pointer at apCraft[nPlaneSelCraftIdx], with no null test, and indexes aItemQty with the
 * unbounded dword craft index; the port skips a null entry and resolves an out-of-range craft index to zero /
 * no craft. The value string is built by itoa into var_34 @ sp+0x48, a byte-copy loop into psz @ sp+0x10
 * (0x7D590, unbounded), strcat with the backslash and strcat with the second itoa; the port builds the same
 * string in one bounded snprintf over the same 52 / 56-byte sizes. The preview gate reads the status panel's
 * two fields through the global with no null test; the port treats a missing panel as "not armed".
 * BasePlaneScreen_DrawCraftDetail is not ported and enters as a hook, so the detail arm returns the hook's
 * value. */
#define PUFO_PLANE_TEXT_ARMAMENT       146 /* lang.dat "Вооружение" at (25,23), value at (190,23) */
#define PUFO_PLANE_TEXT_CREW           147 /* "Экипаж" at (25,43), value at (195,43) */
#define PUFO_PLANE_TEXT_CREW_QTY       148 /* "ТОП кол-во" at (25,63), value at (195,63) */
#define PUFO_PLANE_TEXT_CRAFT_CAPTION  124 /* "Корабль: " at (25,90) */
#define PUFO_PLANE_TEXT_STATUS_CAPTION 153 /* "Статус" at (165,90) */
#define PUFO_PLANE_WEAPON_SLOT_FIRST    25 /* the six weapon slots v3 sums, the SelectWeapon25..30 band */
#define PUFO_PLANE_WEAPON_SLOT_LAST     30
#define PUFO_PLANE_QTY_SEP             "\\" /* asc_AB9F0 @ 0xAB9F0: one backslash */
#define PUFO_PLANE_ITOA_BUF             52  /* var_34 @ sp+0x48, the j_itoa buffer */
#define PUFO_PLANE_VALUE_BUF            56  /* psz @ sp+0x10, up to var_34 */
#define PUFO_PLANE_INNER_X_FIRST        15  /* rail C's climbing x, and rail D's left edge */
#define PUFO_PLANE_INNER_X_DESCEND     220  /* rail C's descending x */
#define PUFO_PLANE_INNER_X_STOP        115  /* rail C stops when the descending x reaches 115 */
#define PUFO_PLANE_INNER_X_RIGHT       224  /* rail D's right edge */
#define PUFO_PLANE_INNER_Y_TOP          15  /* rail C's two rows, and rail D's climbing row base */
#define PUFO_PLANE_INNER_Y_BOTTOM       85
#define PUFO_PLANE_INNER_D_Y_BASE       83  /* rail D's descending row is m + 83 */
#define PUFO_PLANE_INNER_D_STOP         -36 /* rail D stops when m reaches -36 */

typedef struct PlaneDrawEnv {
    const GfxColorEnv *pColor;       /* pGX @ 0x1139B8, the colour-pair owner */
    const u8 *pbShowCraftDetail;     /* bPlaneShowCraftDetail @ 0x161F20, gate 1 */
    const u8 *pbPanelsBuilt;         /* bPlanePanelsBuilt @ 0x161F08, gate 2 */
    const u8 *pbPreviewActive;       /* bBaseFacPreviewActive @ 0x18652C, gate 3 */
    const i32 *pnCursorY;            /* nSmapCursorY @ 0x186BD4: a 16-bit value, read LDRSH */
    UiPanel *pCraftPanel;            /* pPlaneCraftPanel @ 0x161EFC */
    UiPanel *pStatusPanel;           /* pPlaneCraftStatusPanel @ 0x161ED0, the band gate's panel */
    Ship *const *apCraft;            /* apCraft @ 0x114A64, the context's +0x10AC */
    i32 nCraftCount;                 /* Map_nCraftCount @ 0x114B4C: the original never bounds the craft
                                      * index, the port bounds it here */
    const i32 *pnSelCraftIdx;        /* nPlaneSelCraftIdx @ 0x161F04 */
    ObjectRecFull *const *apObjects; /* apObjects @ 0x113AC0, the context's +0x108 */
    i32 nObjectCount;                /* Map_nObjectCount @ 0x114A60, read LDRH */
    Soldier **apSoldiers;            /* the context's +0x1198 */
    i32 nSoldierCount;               /* nSoldierCount @ 0x115350, read LDRH */
    const char *pTextRecs;           /* aTextRecs @ 0xB38D0, stride PUFO_TEXT_REC_STRIDE */
    i32 (*pfnDrawCraftDetail)(void);  /* BasePlaneScreen_DrawCraftDetail, not ported: named without a
                                       * citation so the parity report does not credit it */
} PlaneDrawEnv;

i32 BasePlaneScreen_Draw(const PlaneDrawEnv *pEnv);

#endif /* PUFO_PLANE_H */
