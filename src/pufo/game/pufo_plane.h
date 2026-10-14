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
 * 0x50/0x63. The row count is the global the port calls g_nKeyAccum in the app shell; here it is
 * read as a row count, so "write-only residue" is not the whole story for that global.
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

#endif /* PUFO_PLANE_H */
