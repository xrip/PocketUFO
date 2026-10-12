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

typedef struct PlaneWeaponPanels {
    u8 nPanelASel;  /* unk_161F10, the weapon the first panel has selected */
    u8 nPanelBSel;  /* unk_161F14, the weapon the second panel has selected */
    u8 bPanelAOpen; /* unk_161F18, gate for the first panel */
    u8 bPanelBOpen; /* unk_161F1C, gate for the second panel */
} PlaneWeaponPanels;

i32 BasePlaneScreen_SelectWeapon(PlaneWeaponPanels *pPanels, i32 nWeaponIndex,
                                 i32 (*pfnApplySwap)(void *pUser), void *pUser);

#endif /* PUFO_PLANE_H */
