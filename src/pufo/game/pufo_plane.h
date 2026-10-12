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

#endif /* PUFO_PLANE_H */
