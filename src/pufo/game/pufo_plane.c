/* pufo_plane.c - see pufo_plane.h for evidence. */
#include "game/pufo_plane.h"

/* One body for BasePlaneScreen_SelectWeapon25..30; only the constant differs. */
i32 BasePlaneScreen_SelectWeapon(PlaneWeaponPanels *pPanels, i32 nWeaponIndex,
                                 i32 (*pfnApplySwap)(void *pUser), void *pUser)
{
    const u8 bWeapon = (u8)nWeaponIndex;

    if (pPanels != NULL) {
        /* LDRB the gate, STRBNE the selection: a closed panel keeps its old value. */
        if (pPanels->bPanelAOpen != 0) {
            pPanels->nPanelASel = bWeapon; /* 0x79B34 */
        }
        if (pPanels->bPanelBOpen != 0) {
            pPanels->nPanelBSel = bWeapon; /* 0x79B4C */
        }
    }

    /* 0x79B54 is `B BasePlaneScreen_ApplyWeaponSwap`, so the swap always runs and its
     * result is the handler's result. */
    return (pfnApplySwap != NULL) ? pfnApplySwap(pUser) : 0;
}
