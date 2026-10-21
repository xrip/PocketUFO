/* pufo_plane.c - see pufo_plane.h for evidence. */
#include "game/pufo_plane.h"
#include "game/pufo_geoscape.h"   /* BaseRec_AddItemQty */
#include "game/pufo_font.h"       /* Text_DrawString / Text_MeasureWidth, the panel headers */
#include "game/pufo_baseui.h"      /* Soldier_FormatDisplayName, the crew status column */

#include <stdio.h>   /* snprintf, the row name copy */
#include <string.h>  /* strlen, the 17-character cut at 0x7B838 */

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

/* The five weapons that move ammo to and from base stores, shared by the unload and load
 * passes. nRound is the carried units per base item: 10 for weapon 29, 1 for the rest. */
static const struct PlaneWeaponStore {
    i32 nWeapon;
    i32 nObjectIdx;
    i32 nLoad;
    i32 nRound;
} g_aWeaponStores[5] = {
    { 25,  52, 10, 1 },
    { 26,  53,  3, 1 },
    { 27,  54,  2, 1 },
    { 29, 105, 10, 10 },
    { 30,  55,  2, 1 },
};

static const struct PlaneWeaponStore *WeaponStoreFind(i32 nWeapon)
{
    i32 i;
    for (i = 0; i < 5; ++i) {
        if (g_aWeaponStores[i].nWeapon == nWeapon) {
            return &g_aWeaponStores[i];
        }
    }
    return NULL;
}

/* The carried byte in Ship, selected by weapon. */
static u8 *WeaponStoreAmmo(Ship *pCraft, i32 nWeapon)
{
    switch (nWeapon) {
    case 25: return &pCraft->bAmmo25;
    case 26: return &pCraft->bAmmo26;
    case 27: return &pCraft->bAmmo27;
    case 29: return &pCraft->bAmmo29;
    default: return &pCraft->bAmmo30;
    }
}

/* BasePlane_UnloadWeaponStores @ 0x790e8. Take min(stock, load) of this weapon's object out
 * of base stores and into the selected craft. The original writes the five weapons out as
 * separate branches with the comparison flipped between them; they are the same rule. */
i32 BasePlane_UnloadWeaponStores(i32 nWeapon, const PlaneWeaponEnv *pEnv)
{
    const struct PlaneWeaponStore *pEntry;
    Ship *pCraft;
    ObjectRecFull *pItem;
    BaseRec *pBase;
    u8 *pbAmmo;
    i32 nStock;
    i32 nTake;

    if (pEnv == NULL || pEnv->apCraft == NULL || pEnv->apObjects == NULL ||
        pEnv->apBases == NULL) {
        return 0;
    }

    pEntry = WeaponStoreFind(nWeapon);
    if (pEntry == NULL) {
        return 0; /* 0x79118: an unhandled weapon changes nothing */
    }

    pCraft = pEnv->apCraft[pEnv->nCraftIdx];
    pItem = pEnv->apObjects[pEntry->nObjectIdx];
    pBase = pEnv->apBases[pEnv->nBaseIdx];

    if (pCraft == NULL || pItem == NULL || pBase == NULL) {
        return 0;
    }

    nStock = pItem->auPerBaseVal[(u8)pBase->bSlot];
    nTake = (nStock < pEntry->nLoad) ? nStock : pEntry->nLoad;

    pbAmmo = WeaponStoreAmmo(pCraft, nWeapon);
    *pbAmmo = (u8)(*pbAmmo + nTake * pEntry->nRound); /* 0x79374, 0x79220 and friends */

    /* BaseRec_AddItemQty with a negated delta is exactly what BaseRec_SubItemQty @ 0x5571c
     * does, and the port already has the public one. */
    BaseRec_AddItemQty(pBase, pItem, -nTake); /* 0x793C4 */

    return nTake;
}

/* BasePlane_LoadWeaponStores @ 0x793e0. The reverse: put carried ammo back into base stores.
 *
 * A mode decides how much comes back, and it is computed from the two panel gates before the
 * weapon is looked at. If panel A's gate is set the mode is 2, unless this weapon is the
 * craft's RIGHT-hand weapon, in which case it drops to 1. If panel B's gate is set the mode is
 * 2, unless this weapon is the craft's LEFT-hand weapon, in which case it drops to 1. The
 * second test overwrites the first, so when both gates are set only the second one matters.
 * With neither gate the mode stays 0.
 *
 * Mode 2 means the weapon is being replaced outright: leave one load on the craft and return
 * only the excess above it. Any other mode returns the entire carried amount and zeroes the
 * byte. Weapon 29 converts between rounds and base items on both paths. */
i32 BasePlane_LoadWeaponStores(i32 nWeapon, const PlaneWeaponEnv *pEnv,
                               const PlaneWeaponPanels *pPanels)
{
    const struct PlaneWeaponStore *pEntry;
    Ship *pCraft;
    ObjectRecFull *pItem;
    BaseRec *pBase;
    u8 *pbAmmo;
    i32 nMode = 0;
    i32 nCarried;
    i32 nAdd;

    if (pEnv == NULL || pPanels == NULL || pEnv->apCraft == NULL ||
        pEnv->apObjects == NULL || pEnv->apBases == NULL) {
        return 0;
    }

    pEntry = WeaponStoreFind(nWeapon);
    if (pEntry == NULL) {
        return 0; /* 0x79490 */
    }

    pCraft = pEnv->apCraft[pEnv->nCraftIdx];
    if (pCraft == NULL) {
        return 0;
    }

    if (pPanels->bPanelAOpen != 0) { /* 0x79410 */
        nMode = ((char)nWeapon == (char)pCraft->bWeaponR) ? 1 : 2;
    }
    if (pPanels->bPanelBOpen != 0) { /* 0x79440: this one overwrites the previous result */
        nMode = ((char)nWeapon == (char)pCraft->bWeaponL) ? 1 : 2;
    }

    pItem = pEnv->apObjects[pEntry->nObjectIdx];
    pBase = pEnv->apBases[pEnv->nBaseIdx];
    if (pItem == NULL || pBase == NULL) {
        return 0;
    }

    pbAmmo = WeaponStoreAmmo(pCraft, nWeapon);
    nCarried = *pbAmmo;

    if (nMode == 2) {
        i32 nKeep = pEntry->nLoad * pEntry->nRound; /* 100 for weapon 29, its load in rounds */
        i32 nExcess = (nCarried > nKeep) ? nCarried - nKeep : 0;
        *pbAmmo = (u8)(nCarried - nExcess); /* 0x796F8: the craft keeps one load */
        nAdd = nExcess / pEntry->nRound;    /* 0x79560: back to base items */
    } else {
        *pbAmmo = 0;                        /* 0x79740 */
        nAdd = nCarried / pEntry->nRound;
    }

    BaseRec_AddItemQty(pBase, pItem, nAdd); /* 0x79700 */
    return nAdd;
}

/* The object table the weapon indices address. The original never bounds a weapon index against
 * it here, so the port bounds it only to keep a negative i8 weapon from reading out of range. */
#define PUFO_PLANE_OBJECT_LIMIT 256

/* One side of BasePlaneScreen_ApplyWeaponSwap @ 0x79748. The original writes the left and right
 * halves out longhand; they are the same code with bWeaponL/bWeaponR and the two selection
 * globals swapped, so the port keeps one helper and calls it twice.
 *
 * The order matters and is not the obvious one. When the craft already carries a weapon:
 *
 *   1. put one of the OLD weapon back into base stores
 *   2. return the OLD weapon's carried ammo to base stores
 *   3. take one of the NEW weapon OUT of base stores
 *   4. add the NEW weapon to the craft's cargo
 *   5. load the NEW weapon's ammo from base stores into the craft
 *   6. remove one of the OLD weapon from the craft's cargo
 *
 * A weapon and its ammo are SEPARATE object records: the swap moves apObjects[weapon] through
 * cargo and base stores, while the two store functions move apObjects[52..55] and [105]. So the
 * take-out running before the load does NOT reduce the ammo the load sees; they touch different
 * records. The order that is load-bearing is the weapon-byte assignment, which comes last: the
 * cargo removal at 0x79838 re-reads the weapon byte, and assigning it earlier would remove the
 * NEW weapon from the craft instead of the old one.
 *
 * When the craft carries nothing, steps 1, 2 and 6 are absent and the base-store call for the
 * new weapon moves to the END, after the load rather than before it. The original's call to
 * BasePlane_LoadWeaponStores in that branch passes weapon index 0, which is not one of the five
 * handled weapons and therefore does nothing; the port omits the call and says so. */
static void PlaneSwapSide(Ship *pCraft, ObjectRecFull *const *apObjects, BaseRec *pBase,
                          i32 nNewWeapon, i8 *pbWeapon, const PlaneWeaponEnv *pEnv,
                          const PlaneWeaponPanels *pPanels)
{
    i32 nOld = (i32)*pbWeapon;
    ObjectRecFull *pNew;
    ObjectRecFull *pOld;

    if (nNewWeapon < 0 || nNewWeapon >= PUFO_PLANE_OBJECT_LIMIT) {
        return;
    }
    pNew = apObjects[nNewWeapon];
    if (pNew == NULL) {
        return;
    }

    if (nOld != 0) {
        if (nOld < 0 || nOld >= PUFO_PLANE_OBJECT_LIMIT || apObjects[nOld] == NULL) {
            return;
        }
        pOld = apObjects[nOld];

        BaseRec_AddItemQty(pBase, pOld, 1);              /* 0x797B0 */
        BasePlane_LoadWeaponStores(nOld, pEnv, pPanels); /* 0x797C4 */
        BaseRec_AddItemQty(pBase, pNew, -1);             /* 0x797F0, BaseRec_SubItemQty */
        InvSel_AddItemQty(pCraft, pNew, 1);              /* 0x79810 */
        BasePlane_UnloadWeaponStores(nNewWeapon, pEnv);  /* 0x79818 */
        InvSel_SubItemQty(pCraft, pOld, 1);              /* 0x79838 */
    } else {
        /* 0x79854 would call LoadWeaponStores(0), which handles nothing. */
        InvSel_AddItemQty(pCraft, pNew, 1);              /* 0x79878 */
        BasePlane_UnloadWeaponStores(nNewWeapon, pEnv);  /* 0x79880 */
        BaseRec_AddItemQty(pBase, pNew, -1);             /* 0x798A8, and this one comes LAST */
    }

    *pbWeapon = (i8)nNewWeapon; /* 0x798BC */
}

/* BasePlaneScreen_ApplyWeaponSwap @ 0x79748. Applies the loadout the two weapon panels selected.
 * Both gate flags are cleared at the end whether or not either side did anything, and the
 * original always returns 0. */
i32 BasePlaneScreen_ApplyWeaponSwap(PlaneWeaponPanels *pPanels, const PlaneWeaponEnv *pEnv)
{
    Ship *pCraft;
    BaseRec *pBase;

    if (pPanels == NULL || pEnv == NULL || pEnv->apCraft == NULL ||
        pEnv->apObjects == NULL || pEnv->apBases == NULL) {
        return 0;
    }

    pCraft = pEnv->apCraft[pEnv->nCraftIdx];
    pBase = pEnv->apBases[pEnv->nBaseIdx];
    if (pCraft == NULL || pBase == NULL) {
        pPanels->bPanelAOpen = 0; /* 0x79A10 */
        pPanels->bPanelBOpen = 0; /* 0x79A18 */
        return 0;
    }

    if (pPanels->bPanelAOpen != 0) {
        PlaneSwapSide(pCraft, pEnv->apObjects, pBase, pPanels->nPanelASel, &pCraft->bWeaponL,
                      pEnv, pPanels);
    }
    if (pPanels->bPanelBOpen != 0) {
        PlaneSwapSide(pCraft, pEnv->apObjects, pBase, pPanels->nPanelBSel, &pCraft->bWeaponR,
                      pEnv, pPanels);
    }

    pPanels->bPanelAOpen = 0; /* 0x79A10 */
    pPanels->bPanelBOpen = 0; /* 0x79A18 */
    return 0;                 /* 0x79A1C */
}

/* BasePlaneScreen_AddInterceptMission @ 0x7de14.
 *
 * IDA says this function takes no arguments. It does. The disassembly opens with
 * `MOV R3, R1, LSL #24` and `MOV R3, R3, ASR #24`, a sign-extend of R1's low byte, and R1 is
 * then used twice: as the index into the mission-site table (`MUL R2, R3, #0x18`, so a 24-byte
 * stride) and again unchanged as the bCraftClass argument.
 *
 * The coordinates are LDRSH loads, so both are signed 16-bit. */
i32 BasePlaneScreen_AddInterceptMission(i32 bCraftClass, const InterceptMissionCtx *pCtx)
{
    StrategItem *pItem;
    i32 nCount;

    if (pCtx == NULL || pCtx->pList == NULL || pCtx->pnZoomMode == NULL ||
        pCtx->pnSeed == NULL || pCtx->apSites == NULL || pCtx->pnMissionId == NULL) {
        return 0;
    }

    /* Deviation: the original indexes with the sign-extended char and never bounds it. */
    if (bCraftClass < 0 || bCraftClass >= pCtx->nSiteCount) {
        return *pCtx->pnMissionId;
    }

    StrategList_AddItem(pCtx->pList, pCtx->pnSeed, *pCtx->pnZoomMode, 3, 1, bCraftClass,
                        pCtx->apSites[bCraftClass].x,  /* 0x7DE48, LDRSH [R0,#0xC8] */
                        pCtx->apSites[bCraftClass].y,  /* 0x7DE3C, LDRSH [R0,#0xCA] */
                        *pCtx->pnMissionId, 1);        /* 0x7DE64 */

    nCount = PtrArray_GetCount(pCtx->pList);           /* 0x7DE6C */
    pItem = (StrategItem *)PtrArray_Get(pCtx->pList, nCount - 1);
    if (pItem != NULL) {
        pItem->nTimer = 2880;                          /* 0x7DE80 */
    }

    if (pCtx->pnLastIndex != NULL) {
        /* The count is read a third time here rather than reused from 0x7DE6C. */
        *pCtx->pnLastIndex = PtrArray_GetCount(pCtx->pList) - 1; /* 0x7DE98 */
    }

    *pCtx->pnMissionId = *pCtx->pnMissionId + 1;       /* 0x7DEAC */
    return *pCtx->pnMissionId;                         /* the incremented value */
}

/* ------------------------------------------------- the loadout screen's two stat panels */

static const char *PlaneLabel(const u8 *pTextRecs, i32 nRec)
{
    return (const char *)(pTextRecs + PUFO_TEXT_REC_STRIDE * nRec);
}

void BasePlaneScreen_DrawStatLabel(PlaneWeaponPanels *pPanels, i32 nPanel, u8 *pTextRecs,
                                   const GfxColorEnv *pColor, i32 nRowCount)
{
    if (pPanels == NULL || pTextRecs == NULL) {
        return;
    }

    if (nPanel == 0) {
        pPanels->bPanelAOpen = 1;                     /* 0x79E28, DrawStatLabelA */
    } else {
        pPanels->bPanelBOpen = 1;                     /* 0x79FEC, DrawStatLabelB */
    }

    SoftSurf_BlitRectProxy(0, 5, 75, 230, 20 * (nRowCount + 1), 0x44, 0xF4, 0xFC); /* 0x79E54 */
    SoftSurf_BlitRectProxy(0, 6, 76, 228, 20 * nRowCount + 18, 0x14, 0x50, 0x63);   /* 0x79E8C */
    SoftSurf_BlitRectProxy(0, 7, 77, 226, 20 * nRowCount + 16, 0x14, 0x50, 0x63);   /* 0x79EBC */
    (void)Gfx_PackRgb565Pair(pColor, 68, 244, 252, 4, 70, 75);                      /* 0x79EE8 */

    i32 nWidth = Text_MeasureWidth(PlaneLabel(pTextRecs, PUFO_PLANE_LABEL_REC_A));  /* 0x79F08 */
    Text_DrawString(70 - nWidth / 2, 75, PlaneLabel(pTextRecs, PUFO_PLANE_LABEL_REC_A)); /* 0x79F20 */

    pTextRecs[PUFO_TEXT_REC_STRIDE * PUFO_PLANE_LABEL_REC_B + PUFO_PLANE_LABEL_B_KEEP] = 0; /* 0x79F34 */
    nWidth = Text_MeasureWidth(PlaneLabel(pTextRecs, PUFO_PLANE_LABEL_REC_B));      /* 0x79F44 */
    Text_DrawString(164 - nWidth / 2, 75, PlaneLabel(pTextRecs, PUFO_PLANE_LABEL_REC_B)); /* 0x79F64 */

    pTextRecs[PUFO_TEXT_REC_STRIDE * PUFO_PLANE_LABEL_REC_C + PUFO_PLANE_LABEL_C_KEEP] = 0; /* 0x79F78 */
    nWidth = Text_MeasureWidth(PlaneLabel(pTextRecs, PUFO_PLANE_LABEL_REC_C));      /* 0x79F90 */
    Text_DrawString(210 - nWidth / 2, 75, PlaneLabel(pTextRecs, PUFO_PLANE_LABEL_REC_C)); /* 0x79FB0 */
}

/* ------------------------------------------------- putting carried weapon ammo back */

static void Plane_RearmOneSide(PlaneWeaponEnv *pEnv, const PlaneWeaponPanels *pPanels, i32 nSide)
{
    Ship *pShip = (pEnv->apCraft != NULL) ? pEnv->apCraft[pEnv->nCraftIdx] : NULL;

    if (pShip == NULL) {
        return;
    }

    i8 *pbWeapon = (nSide == 0) ? &pShip->bWeaponL : &pShip->bWeaponR;

    for (i32 i = 0; i < pEnv->nObjectCount; ++i) {
        ObjectRecFull *pItem = (pEnv->apObjects != NULL) ? pEnv->apObjects[i] : NULL;

        if (pItem == NULL || pItem->aItemQty[pEnv->nCraftIdx] == 0) {
            continue;                                   /* 0x78F3C / 0x7901C: the craft carries nothing */
        }
        if (i != (i32)*pbWeapon) {
            continue;                                   /* 0x78F54 / 0x79034 */
        }

        BaseRec_AddItemQty(pEnv->apBases[pEnv->nBaseIdx], pItem, 1);      /* 0x78F78 / 0x79058 */
        (void)InvSel_SubItemQty(pShip, pItem, 1);                         /* 0x78F98 / 0x79078 */
        (void)BasePlane_LoadWeaponStores(i, pEnv, pPanels);               /* 0x78FAC / 0x7908C */
        *pbWeapon = 0;                                                    /* 0x78FBC / 0x7909C */
    }
}

i32 BasePlane_RearmFromStores(PlaneWeaponEnv *pEnv, PlaneWeaponPanels *pPanels)
{
    if (pEnv == NULL || pPanels == NULL) {
        return 0;
    }

    if (pPanels->bPanelAOpen != 0) {
        Plane_RearmOneSide(pEnv, pPanels, 0);           /* 0x78F0C: the left-hand weapon */
    }
    if (pPanels->bPanelBOpen != 0) {
        Plane_RearmOneSide(pEnv, pPanels, 1);           /* 0x78FEC: the right-hand weapon */
    }

    pPanels->bPanelAOpen = 0;                           /* 0x790C4 */
    pPanels->bPanelBOpen = 0;                           /* 0x790CC */
    return pEnv->nObjectCount;                          /* 0x790D0: the count read at entry */
}

/* ------------------------------------------------- closing the loadout screen */

void BasePlaneScreen_Close(const PlaneCloseEnv *pEnv)
{
    /* slot, object slot, cargo column, base delta, cargo delta. Slot 33 has no arm in the original's
     * 31..35 loop. Object 105 is weapon 29's ammo, counted in rounds of ten per base item, which is
     * why its two deltas differ by ten - the same 5-for-50 BasePlaneScreen_CommitStores runs inbound. */
    static const struct {
        i32 nSlot; i32 nObjSlot; i32 nCargoCol; i32 nBaseMul; i32 nCargoMul;
    } aRows[4] = {
        { 31,  56, 0, 30, 30 },
        { 32,  57, 1,  8,  8 },
        { 34, 105, 2,  5, 50 },
        { 35,  58, 3,  8,  8 },
    };

    if (pEnv == NULL) {
        return;
    }

    if (pEnv->bStoresCommitted != 0) {
        BaseRec *pBase = (pEnv->apBases != NULL) ? pEnv->apBases[pEnv->nBaseIdx] : NULL;
        Ship *pShip = (pEnv->apCraft != NULL) ? pEnv->apCraft[pEnv->nCraftIdx] : NULL;

        for (i32 r = 0; r < 4; ++r) {
            const i32 nCount = (pEnv->anSlotCount != NULL) ? pEnv->anSlotCount[aRows[r].nSlot] : 0;
            ObjectRecFull *pItem = (pEnv->apObjects != NULL) ? pEnv->apObjects[aRows[r].nObjSlot] : NULL;

            if (nCount == 0 || pItem == NULL || pBase == NULL || pShip == NULL) {
                continue; /* deviation: the original tests none of these */
            }

            BaseRec_AddItemQty(pBase, pItem, aRows[r].nBaseMul * nCount);
            pShip->anCargoQty[aRows[r].nCargoCol] -= aRows[r].nCargoMul * nCount;
        }
    }

    if (pEnv->pnCrewQuota != NULL) {
        *pEnv->pnCrewQuota = 0;                         /* 0x7CBAC */
    }
    if (pEnv->pfnCloseUi != NULL) {
        pEnv->pfnCloseUi(pEnv->pUser);                  /* 0x7CBB4 */
    }
}

/* ------------------------------------------------- the hangar stores list */

/* The four crew rows, in the order the original's 31..35 switch walks them. Slot 33 is in neither
 * switch: it has no object record, so it clears every gate and moves nothing. */
static const struct PlaneStoreRow {
    i32 nSlot;
    i32 nObject;
    i32 nCargoCol;
    i32 nBaseDelta;  /* items the base moves */
    i32 nCargoDelta; /* cargo units the craft moves */
} g_aStoreRows[4] = {
    { 31,  56, 0, 30, 30 },
    { 32,  57, 1,  8,  8 },
    { 34, 105, 2,  5, 50 },
    { 35,  58, 3,  8,  8 },
};

static const struct PlaneStoreRow *StoreRowFind(i32 nSlot)
{
    for (i32 i = 0; i < 4; ++i) {
        if (g_aStoreRows[i].nSlot == nSlot) {
            return &g_aStoreRows[i];
        }
    }
    return NULL;
}

static ObjectRecFull *StoreRowObject(const PlaneStoresEnv *pEnv, i32 nObject)
{
    return (pEnv->apObjects != NULL && nObject >= 0 && nObject < pEnv->nObjectCount)
               ? pEnv->apObjects[nObject]
               : NULL;
}

i32 BasePlaneScreen_CommitStores(PlaneStoresEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pCommitPanel == NULL || pEnv->pSlotPanel == NULL ||
        pEnv->pAvailPanel == NULL || pEnv->pTakenPanel == NULL ||
        pEnv->anSlotCarryCount == NULL || pEnv->pnCrewQuota == NULL) {
        return 0;
    }

    const i32 nRow = pEnv->pCommitPanel->nUnk3032;
    if (nRow < 0 || nRow >= PUFO_PANEL_LINES) {
        return 0; /* deviation: the original indexes anLineAux with no bound */
    }

    const i32 nSlot = pEnv->pSlotPanel->anLineAux[nRow]; /* 0x7AC58 */
    if (pEnv->pAvailPanel->anLineAux[nRow] <= 0) {
        return 0; /* 0x7AC68 */
    }

    const bool bCrewRow = (nSlot > 30 && nSlot < 36);
    Ship *pShip = (pEnv->apCraft != NULL) ? pEnv->apCraft[pEnv->nCraftIdx] : NULL;
    BaseRec *pBase = (pEnv->apBases != NULL) ? pEnv->apBases[pEnv->nBaseIdx] : NULL;

    if (!bCrewRow) {
        /* 0x7AD38..0x7AE50: what is already carried, plus this one unit, against the hold cap. */
        const i32 nPending = BasePlane_CalcWeaponStoresUsed(pEnv->apObjects, pEnv->nObjectCount,
                                                            pEnv->anSlotCarryCount);
        const i32 nCarried = Base_CalcWeaponStorageUsed(pEnv->apObjects, pEnv->nObjectCount,
                                                        pEnv->nCraftIdx);
        const ObjectRecFull *pItem = StoreRowObject(pEnv, nSlot);
        const i32 nSize = (pItem != NULL) ? (i32)pItem->bCargoSize : 0;

        if (nPending + nSize + nCarried > (i32)Craft_GetTypeCargoCap(pShip)) {
            if (pEnv->pbShortfallFlag != NULL) {
                *pEnv->pbShortfallFlag = 1; /* 0x7AE54: a dword store into a byte global */
            }
            if (pEnv->pnResidueFlag != NULL) {
                *pEnv->pnResidueFlag = 0; /* 0x7AE5C */
            }
            return 0;
        }
    }

    i32 nQuota = *pEnv->pnCrewQuota;
    if (bCrewRow) {
        nQuota = ++(*pEnv->pnCrewQuota); /* 0x7AE88: the quota is spent before the gate is asked */
    }

    const i32 nAboard = Craft_CountSoldiersAboard(pEnv->apSoldiers, pEnv->nSoldierCount,
                                                  pEnv->nCraftIdx); /* 0x7AD38 */

    if (bCrewRow) {
        bool bCrewOk;

        switch (Craft_GetTypeCargoCap(pShip)) {
        case CRAFT_CARGO_CAP_NONE:
        case CRAFT_CARGO_CAP_TYPE1:
            bCrewOk = false; /* cap 0 and cap 120 refuse outright */
            break;
        case CRAFT_CARGO_CAP_TYPE0:
            bCrewOk = (nQuota <= 3 && nAboard + 4 * nQuota <= 14);
            break;
        case CRAFT_CARGO_CAP_TYPE2:
            bCrewOk = (nQuota <= 4 && nAboard + 4 * nQuota <= 26);
            break;
        default:
            bCrewOk = true; /* an unknown craft type has no crew gate at all */
            break;
        }

        if (!bCrewOk) {
            if (pEnv->pbShortfallFlag != NULL) {
                *pEnv->pbShortfallFlag = 1;
            }
            if (pEnv->pnResidueFlag != NULL) {
                *pEnv->pnResidueFlag = 0;
            }
            --(*pEnv->pnCrewQuota); /* 0x7AF98: the spent quota is handed back */
            return 0;
        }

        const struct PlaneStoreRow *pRow = StoreRowFind(nSlot);

        if (pRow != NULL) {
            const i32 nStock = Base_CraftWeaponItemId(pEnv->apObjects, nSlot, (i32)pEnv->nBaseIdx);

            if (nStock < pRow->nBaseDelta) {
                if (pEnv->pbShortfallFlag != NULL) {
                    *pEnv->pbShortfallFlag = 1;
                }
                if (pEnv->pnResidueFlag != NULL) {
                    *pEnv->pnResidueFlag = 2; /* 0x7AF90: 2 is the "not enough stock" reason */
                }
                --(*pEnv->pnCrewQuota);
                return 0;
            }

            BaseRec_SubItemQty(pBase, StoreRowObject(pEnv, pRow->nObject), pRow->nBaseDelta);
            if (pShip != NULL) {
                pShip->anCargoQty[pRow->nCargoCol] += pRow->nCargoDelta;
            }
        }
    }

    pEnv->pTakenPanel->anLineAux[nRow] += 1; /* 0x7AFB4 */
    pEnv->pAvailPanel->anLineAux[nRow] -= 1; /* 0x7AFC4 */
    if (nSlot >= 0 && nSlot < pEnv->nObjectCount) {
        pEnv->anSlotCarryCount[nSlot] += 1; /* 0x7AFD8 */
    }
    return 1;
}

i32 BasePlaneScreen_AdjustSelectedQty(PlaneStoresEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pRowPanel == NULL || pEnv->pSlotPanel == NULL ||
        pEnv->pAvailPanel == NULL || pEnv->pTakenPanel == NULL ||
        pEnv->anSlotCarryCount == NULL) {
        return 0;
    }

    const i32 nRow = pEnv->pRowPanel->nUnk3032;
    if (nRow < 0 || nRow >= PUFO_PANEL_LINES) {
        return 0; /* deviation: the original indexes anLineAux with no bound */
    }

    const i32 nTaken = pEnv->pTakenPanel->anLineAux[nRow]; /* 0x7A9E4 */
    const i32 nSlot = pEnv->pSlotPanel->anLineAux[nRow];   /* 0x7A9F0 */

    if (nTaken <= 0) {
        return 0; /* 0x7A9F8 */
    }

    pEnv->pTakenPanel->anLineAux[nRow] = nTaken - 1; /* 0x7AA08 */
    pEnv->pAvailPanel->anLineAux[nRow] += 1;         /* 0x7AA18 */
    if (nSlot >= 0 && nSlot < pEnv->nObjectCount) {
        pEnv->anSlotCarryCount[nSlot] -= 1;          /* 0x7AA28 */
    }

    if (nSlot <= 30 || nSlot >= 36) {
        return 1;
    }

    if (pEnv->pnCrewQuota != NULL) {
        --(*pEnv->pnCrewQuota); /* 0x7AA50 */
    }

    const struct PlaneStoreRow *pRow = StoreRowFind(nSlot);

    if (pRow == NULL) {
        return 1; /* slot 33: the quota moved, no stock did */
    }

    BaseRec_AddItemQty((pEnv->apBases != NULL) ? pEnv->apBases[pEnv->nBaseIdx] : NULL,
                       StoreRowObject(pEnv, pRow->nObject), pRow->nBaseDelta);

    Ship *pShip = (pEnv->apCraft != NULL) ? pEnv->apCraft[pEnv->nCraftIdx] : NULL;

    if (pShip != NULL) {
        /* 0x7AB10 and friends: here the debit IS clamped at zero, which BasePlaneScreen_Close is not. */
        const i32 nCargo = pShip->anCargoQty[pRow->nCargoCol];
        pShip->anCargoQty[pRow->nCargoCol] = (nCargo < pRow->nCargoDelta) ? 0 : nCargo - pRow->nCargoDelta;
    }

    return 1;
}

/* BuySellScreen_ApplyQtyChanges @ 0x78d90. Commit what the hangar rows staged. */
void BuySellScreen_ApplyQtyChanges(const BuySellApplyQtyEnv *pEnv)
{
    if (pEnv != NULL && pEnv->anSlotCarryCount != NULL && pEnv->apObjects != NULL) {
        BaseRec *const *apBases = pEnv->apBases;
        Ship *const *apCraft = pEnv->apCraft;
        BaseRec *pBase = (apBases != NULL && pEnv->nBaseIdx >= 0 &&
                          pEnv->nBaseIdx < PUFO_FAC_BASE_COUNT)
                             ? apBases[pEnv->nBaseIdx]
                             : NULL;
        Ship *pShip = (apCraft != NULL && pEnv->nCraftIdx >= 0 &&
                       pEnv->nCraftIdx < pEnv->nCraftCount)
                          ? apCraft[pEnv->nCraftIdx]
                          : NULL;

        for (i32 i = 0; i < pEnv->nObjectCount; ++i) { /* 0x78DC0..0x78E50 */
            ObjectRecFull *pItem = pEnv->apObjects[i]; /* Map + i * 4 + 0x108 */
            const i32 nDelta = pEnv->anSlotCarryCount[i]; /* 0x78DF4 */

            if (nDelta < 0) { /* 0x78DF8 CMP R2,#0 / BPL */
                if (pBase != NULL) {
                    BaseRec_AddItemQty(pBase, pItem, -nDelta); /* 0x78E00 */
                }
                /* 0x78E1C re-reads the array and negates again rather than reusing v4. */
                (void)InvSel_SubItemQty(pShip, pItem, (i16)(-pEnv->anSlotCarryCount[i]));
            } else {
                if (pBase != NULL) {
                    BaseRec_SubItemQty(pBase, pItem, nDelta); /* 0x78E34 */
                }
                (void)InvSel_AddItemQty(pShip, pItem, (i16)nDelta); /* 0x78E4C */
            }
        }
    }

    if (pEnv != NULL && pEnv->pfnCloseUi != NULL) {
        pEnv->pfnCloseUi(pEnv->pUser); /* 0x78E74, tail call into Geoscape_CloseUI */
    }
}

/* ------------------------------------------------- the crew list's click */

static void PanelLineCopy(UiPanel *pPanel, i32 nLine, const char *psz)
{
    if (nLine < 0 || nLine >= PUFO_PANEL_LINES) {
        return;
    }

    u8 *pLine = pPanel->apLines[nLine];

    if (pLine == NULL) {
        return;
    }

    i32 i = 0;

    while (psz[i] != 0 && i < PUFO_PANEL_LINE_BYTES - 1) {
        pLine[i] = (u8)psz[i];
        ++i;
    }
    pLine[i] = 0;
}

void BasePlaneScreen_FillCrewPanel(PlaneCrewPanelEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pCrewPanel == NULL || pEnv->pStatusPanel == NULL) {
        return;
    }

    const i32 nRow = pEnv->pCrewPanel->nUnk3032;

    if (nRow < 0 || nRow >= PUFO_PANEL_LINES) {
        return; /* deviation: the original has no bound */
    }

    const i32 nSol = pEnv->pCrewPanel->anLineAux[nRow];
    Soldier *pSol = (pEnv->apSoldiers != NULL && nSol >= 0 && nSol < pEnv->nSoldierCount)
                        ? pEnv->apSoldiers[nSol]
                        : NULL;

    if (pSol == NULL) {
        return; /* deviation: the original faults */
    }

    if (pSol->bUnk183 == PUFO_SOLDIER_CRAFT_NONE) {
        const Ship *pShip = (pEnv->apCraft != NULL) ? pEnv->apCraft[pEnv->nCraftIdx] : NULL;
        const i32 nAboard = Craft_CountSoldiersAboard(pEnv->apSoldiers, pEnv->nSoldierCount,
                                                      pEnv->nCraftIdx); /* 0x7A1E8 */
        const i32 nWeapons = Craft_CountWeaponSlotsCarried(pEnv->apObjects, pEnv->nObjectCount,
                                                           pEnv->nCraftIdx); /* 0x7A1F0 */
        bool bRoom;

        switch ((pShip != NULL) ? (i32)pShip->bCraftType : -1) {
        case 0:
            bRoom = (nAboard + 4 * nWeapons + 1 <= 14); /* 0x7A21C */
            break;
        case 1:
            bRoom = (nAboard + 1 <= 12);                /* 0x7A230 */
            break;
        case 2:
            bRoom = (nAboard + 4 * nWeapons + 1 <= 26); /* 0x7A250 */
            break;
        default:
            bRoom = true; /* 0x7A268: any other craft type has no crew gate here */
            break;
        }

        if (!bRoom) {
            return; /* 0x7A27C: refusing changes nothing at all */
        }

        UiPanel_SetLineMeta(pEnv->pCrewPanel, nRow, 255, 200, 20, 10, 10, 10);   /* 0x7A2C0 */
        UiPanel_SetLineMeta(pEnv->pStatusPanel, nRow, 255, 200, 20, 10, 10, 10); /* 0x7A2E8 */
        pSol->bUnk183 = (u8)pEnv->nCraftIdx;                                     /* 0x7A2F8 */

        char *pszName = (pEnv->pszNameBuf != NULL)
                            ? Soldier_FormatDisplayName(pSol, pEnv->apCraft, pEnv->pTextRecs,
                                                        pEnv->pszNameBuf, (size_t)pEnv->cchNameBuf)
                            : (char *)"";
        PanelLineCopy(pEnv->pStatusPanel, nRow, pszName); /* 0x7A310 */
        return;
    }

    UiPanel_SetLineMeta(pEnv->pCrewPanel, nRow, 240, 240, 240, 20, 20, 20);   /* 0x7A338 */
    UiPanel_SetLineMeta(pEnv->pStatusPanel, nRow, 240, 240, 240, 20, 20, 20); /* 0x7A360 */
    PanelLineCopy(pEnv->pStatusPanel, nRow,
                  (const char *)(pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * PUFO_PLANE_CREW_UNASSIGNED_REC));
    pSol->bUnk183 = PUFO_SOLDIER_CRAFT_NONE; /* 0x7A388: the original stores -1 into the byte */
}

/* ------------------------------------------------- building the crew list's two panels */

/* g_aPlaneListNav @ 0x000ABC48: 2 UiControlDesc records. BasePlaneScreen_BuildListPanels @ 0x7a3f4,
 * the crew list prev/next pair. Printed by tools/gen_geoiinit_tables.py. */
static const UiControlDesc g_aPlaneListNav[PUFO_PLANE_LIST_NAV] = {
    { 205, 20, "up", "sup", "", 0 },
    { 205, 40, "down", "sdown", "", 0 }
};

static UiPanel *PlanePanelNew(i32 nLineCount)
{
    UiPanel *pPanel = (UiPanel *)malloc(sizeof(UiPanel));

    if (pPanel == NULL) {
        return NULL; /* 0x7A4EC: operator new may return null and the original stores it anyway */
    }
    if (!UiPanel_ctor(pPanel, nLineCount)) {
        free(pPanel); /* deviation: the original keeps a panel whose lines failed to allocate */
        return NULL;
    }
    return pPanel;
}

static void PlanePanelFree(UiPanel *pPanel)
{
    if (pPanel == NULL) {
        return;
    }
    UiPanel_FreeLines(pPanel); /* 0x7A468 / 0x7A4B0 */
    free(pPanel);
}

static void PlaneListStyle(UiPanel *pPanel)
{
    pPanel->nStyle = 14;   /* 0x7A554 / 0x7A62C: fourteen visible rows */
    pPanel->nFont = 16;
    pPanel->nPosY = 70;
    pPanel->nRectX = 5;
    pPanel->nRectY = 70;
    UiPanel_SetBytes0to2(pPanel, 0xB4, 0xB4, 0xB4);
    pPanel->bColorKey = 0x96;
    UiPanel_SetBytes6to11(pPanel, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);
    UiPanel_SetBytes3to5(pPanel, 0x32, 0x32, 0xFA);
}

void BasePlaneScreen_BuildListPanels(PlaneListPanels *pOut, const PlaneListPanelsEnv *pEnv)
{
    if (pOut == NULL || pEnv == NULL || pEnv->pbBuilt == NULL) {
        return;
    }

    if (*pEnv->pbBuilt) {
        *pEnv->pbBuilt = 0; /* 0x7A400: MOV R1,#1 / STRB ... the clear arm writes 0 */

        for (i32 i = 0; i < PUFO_PLANE_LIST_NAV; ++i) {
            if (pOut->apNav[i] != NULL) {
                UiControl_FreePads(pOut->apNav[i]); /* 0x7A418 */
                free(pOut->apNav[i]);
                pOut->apNav[i] = NULL;
            }
        }
        PlanePanelFree(pOut->pCrewPanel);
        PlanePanelFree(pOut->pCrewStatusPanel);
        pOut->pCrewPanel = NULL;
        pOut->pCrewStatusPanel = NULL;
        return;
    }

    *pEnv->pbBuilt = 1; /* 0x7A4BC */

    for (i32 i = 0; i < PUFO_PLANE_LIST_NAV; ++i) {
        pOut->apNav[i] = (UiControl *)malloc(sizeof(UiControl));
        if (pOut->apNav[i] != NULL) {
            UiControl_ctor(pOut->apNav[i]);
        }
    }
    for (i32 i = 0; i < PUFO_PLANE_LIST_NAV; ++i) {
        if (pOut->apNav[i] != NULL) {
            pOut->apNav[i]->pfnClick = pEnv->apfnNavClick[i]; /* 0x7A51C / 0x7A528 */
        }
    }
    for (i32 i = 0; i < PUFO_PLANE_LIST_NAV; ++i) {
        UiControl_Setup(pOut->apNav[i], 0, 0, &g_aPlaneListNav[i], pEnv->pfnLoadPads, pEnv->pPadUser);
    }

    pOut->pCrewPanel = PlanePanelNew(pEnv->nLineCount);

    if (pOut->pCrewPanel != NULL) {
        PlaneListStyle(pOut->pCrewPanel);
        pOut->pCrewPanel->pfnClick = pEnv->pfnCrewClick; /* 0x7A574 */
        pOut->pCrewPanel->nPosX = 10;
        pOut->pCrewPanel->nViewW = 230;
    }

    pOut->pCrewStatusPanel = PlanePanelNew(pEnv->nLineCount);

    if (pOut->pCrewStatusPanel != NULL) {
        PlaneListStyle(pOut->pCrewStatusPanel);
        pOut->pCrewStatusPanel->nPosX = 190;
        pOut->pCrewStatusPanel->nViewW = 30;
        pOut->pCrewStatusPanel->bSelLineHighlight = 0; /* 0x7A664: UiPanel_ctor had set it to 1 */
        pOut->pCrewStatusPanel->nUnk7076 = 1;  /* 0x7A670 */
    }

    if (pOut->pCrewPanel == NULL || pOut->pCrewStatusPanel == NULL) {
        return; /* deviation: the original walks on with a null panel */
    }

    const i32 nScroll = pOut->pCrewPanel->nUnk3028;
    i32 nRow = 0;

    for (i32 i = 0; i < pEnv->nSoldierCount; ++i) {
        Soldier *pSol = (pEnv->apSoldiers != NULL) ? pEnv->apSoldiers[i] : NULL;

        if (pSol == NULL) {
            continue; /* deviation: the original faults */
        }
        if ((i32)pSol->nKind != (i32)pEnv->nBaseIdx) {
            continue; /* 0x7A75C: LDRB against a sign-extended base index, so a negative base matches nothing */
        }

        const i32 nCraft = (i32)pSol->bUnk183;

        if (nCraft != PUFO_SOLDIER_CRAFT_NONE && nCraft != pEnv->nCraftIdx) {
            continue; /* 0x7A770 / 0x7A784 */
        }

        PanelLineCopy(pOut->pCrewPanel, nRow, pSol->szName); /* 0x7A794 */

        /* 0x7A7B8: the status column adds the panel's scroll cursor to the walk index. The panel was
         * allocated earlier in this same call, so the cursor is always zero here and the addition is
         * dead code as written; the port keeps it rather than silently simplifying it away. */
        const i32 nNamed = i + nScroll;
        Soldier *pNamed = (pEnv->apSoldiers != NULL && nNamed >= 0 && nNamed < pEnv->nSoldierCount)
                              ? pEnv->apSoldiers[nNamed]
                              : NULL;
        char *pszName = (pNamed != NULL && pEnv->pszNameBuf != NULL)
                            ? Soldier_FormatDisplayName(pNamed, pEnv->apCraft, pEnv->pTextRecs,
                                                        pEnv->pszNameBuf, (size_t)pEnv->cchNameBuf)
                            : (char *)"";
        PanelLineCopy(pOut->pCrewStatusPanel, nRow, pszName); /* 0x7A7FC */

        if (nCraft == pEnv->nCraftIdx) {
            UiPanel_SetLineMeta(pOut->pCrewPanel, nRow, 255, 200, 20, 10, 10, 10);   /* 0x7A87C */
            UiPanel_SetLineMeta(pOut->pCrewStatusPanel, nRow, 255, 200, 20, 10, 10, 10);
        }
        if (nCraft == PUFO_SOLDIER_CRAFT_ABOARD_OUT) {
            UiPanel_SetLineMeta(pOut->pCrewPanel, nRow, 255, 100, 100, 10, 10, 10);  /* 0x7A8E0 */
            UiPanel_SetLineMeta(pOut->pCrewStatusPanel, nRow, 255, 100, 100, 10, 10, 10);
        }

        if (nRow < PUFO_PANEL_LINES) {
            pOut->pCrewPanel->anLineAux[nRow] = i; /* 0x7A934: what FillCrewPanel will click */
        }
        ++nRow;
    }
}

/* ------------------------------------------------- the hangar craft list */

/* aPlaneCraftTypeOrder @ 0xABE28: five dwords read straight from the IDB (00 00 00 00, 01 ..., 02 ...,
 * 03 ..., 04 ...). The row pass walks this outer loop, which is what groups the list by craft type. */
static const i32 g_aPlaneCraftTypeOrder[PUFO_CRAFT_TYPE_COUNT] = { 0, 1, 2, 3, 4 };

static const char *PlaneTextRec(const PlaneBuildEnv *pEnv, i32 nRec)
{
    return (const char *)(pEnv->pTextRecs + PUFO_TEXT_REC_STRIDE * nRec);
}

/* The two panels share everything except their x origin, their view width, their click and the fact
 * that only panel A gets the SetBytes6to11 call. */
static void PlaneCraftPanelStyle(UiPanel *pPanel)
{
    pPanel->nStyle = 8;    /* 0x7CD44 / 0x7CE68: eight visible rows */
    pPanel->nFont = 16;
    pPanel->nPosY = 110;
    pPanel->nRectX = 5;
    pPanel->nRectY = 110;
    UiPanel_SetBytes0to2(pPanel, 0xB4, 0xB4, 0xB4);
    pPanel->bSelLineHighlight = 1;
    pPanel->bColorKey = 0x96;
}

static void PlaneFreePad(PadImage **ppPad)
{
    if (*ppPad == NULL) {
        return;
    }
    PadImage_FreeData(*ppPad); /* 0x7D290 / 0x7D2E8 / 0x7D318 */
    free(*ppPad);
    *ppPad = NULL;             /* deviation: the original leaves the slot dangling */
}

static void PlaneFreeCtl(UiControl **ppCtl)
{
    if (*ppCtl == NULL) {
        return;
    }
    UiControl_FreePads(*ppCtl); /* 0x7D2B4 / 0x7D34C */
    free(*ppCtl);
    *ppCtl = NULL;
}

void BasePlaneScreen_BuildPanels(PlaneCraftPanels *pOut, const PlaneBuildEnv *pEnv)
{
    if (pOut == NULL || pEnv == NULL || pEnv->pbBuilt == NULL) {
        return;
    }

    if (*pEnv->pbBuilt) {
        *pEnv->pbBuilt = 0; /* 0x7D238 */

        PlanePanelFree(pOut->pCraftPanel);  /* 0x7D244..0x7D258 */
        pOut->pCraftPanel = NULL;
        PlanePanelFree(pOut->pStatusPanel); /* 0x7D264..0x7D278 */
        pOut->pStatusPanel = NULL;

        if (pEnv->pObjects != NULL) {
            BasePlaneScreen *pObj = pEnv->pObjects;

            PlaneFreePad(&pObj->pBackdropPad); /* 0x7D280..0x7D298 */
            for (i32 i = 0; i < 4; ++i) {
                PlaneFreeCtl(&pObj->apMainCtl[i]);   /* 0x7D2A0..0x7D2C4, +0x30..+0x3C */
            }
            for (i32 j = 0; j < 5; ++j) {
                PlaneFreePad(&pObj->apCraftPads[j]); /* 0x7D2D4..0x7D2F4, +0x04..+0x14 */
            }
            for (i32 k = 0; k < 6; ++k) {
                PlaneFreePad(&pObj->apWeaponPads[k]); /* 0x7D304..0x7D328, +0x18..+0x2C */
            }
            for (i32 m = 0; m < 7; ++m) {
                PlaneFreeCtl(&pObj->apWeaponCtl[m]);  /* 0x7D338..0x7D35C, +0x40..+0x58 */
            }
        }
        return;
    }

    *pEnv->pbBuilt = 1; /* 0x7CCAC */

    i32 nCount = (pEnv->pnCraftCount != NULL) ? (i32)*pEnv->pnCraftCount : 0; /* Map_nCraftCount is a byte */
    i32 nRows = 0;

    if (pEnv->apCraft != NULL && pEnv->pnSelectedBaseIdx != NULL) {
        for (i32 i = 0; i < nCount; ++i) { /* 0x7CCD8..0x7CCFC */
            Ship *pShip = pEnv->apCraft[i];

            if (pShip != NULL && (i32)pShip->bHomeBase == (i32)*pEnv->pnSelectedBaseIdx) {
                ++nRows;
            }
        }
    }

    pOut->pCraftPanel = PlanePanelNew(nRows);  /* 0x7CD08..0x7CD3C */
    pOut->pStatusPanel = PlanePanelNew(nRows); /* 0x7CE30..0x7CE60 */

    UiPanel *pCraft = pOut->pCraftPanel;

    if (pCraft != NULL) {
        PlaneCraftPanelStyle(pCraft);
        pCraft->pfnClick = pEnv->pfnCraftClick; /* 0x7CD74 */
        pCraft->nPosX = 10;                     /* 0x7CD88 */
        pCraft->nViewW = 230;                   /* 0x7CDCC */
        UiPanel_SetBytes6to11(pCraft, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14); /* 0x7CDFC */
    }

    UiPanel *pStatus = pOut->pStatusPanel;

    if (pStatus != NULL) {
        PlaneCraftPanelStyle(pStatus);
        pStatus->pfnClick = pEnv->pfnStatusClick; /* 0x7CE8C */
        pStatus->nPosX = 230;                     /* 0x7CEA0 */
        pStatus->nViewW = 30;                     /* 0x7CED8 */
        pStatus->nUnk7076 = 2;                    /* 0x7CF24: right-aligned */
    }

    if (pCraft == NULL || pStatus == NULL || pEnv->apCraft == NULL || pEnv->pnSelectedBaseIdx == NULL
        || pEnv->pTextRecs == NULL || pEnv->pszNameBuf == NULL) {
        return;
    }

    i32 nRow = 0;

    for (i32 t = 0; t < PUFO_CRAFT_TYPE_COUNT; ++t) { /* 0x7CF30: aPlaneCraftTypeOrder, five steps */
        for (i32 i = 0; i < nCount; ++i) {            /* 0x7CF58..0x7D20C */
            Ship *pShip = pEnv->apCraft[i];

            if (pShip == NULL) {
                continue;
            }
            if ((i32)pShip->bHomeBase != (i32)*pEnv->pnSelectedBaseIdx) { /* 0x7CF9C */
                continue;
            }
            if ((i32)pShip->bCraftType != g_aPlaneCraftTypeOrder[t]) {    /* 0x7CF9C */
                continue;
            }

            PanelLineCopy(pCraft, nRow, Ship_FormatDisplayName(pEnv->apCraft, i, pEnv->pTextRecs,
                                                               pEnv->pszNameBuf,
                                                               (size_t)pEnv->cchNameBuf)); /* 0x7CFA8 */
            if (nRow < PUFO_PANEL_LINES) {
                pCraft->anLineAux[nRow] = i; /* 0x7CFE4: the roster index the clicks read */
            }

            if (pShip->bAtBase == 0) { /* 0x7CFF4, the else arm at 0x7D184 */
                PanelLineCopy(pStatus, nRow, PlaneTextRec(pEnv, PUFO_PLANE_STATUS_OUT));
                UiPanel_SetLineMeta(pStatus, nRow, 150, 150, 250, 25, 10, 10); /* 0x7D1D4 */
            } else {
                if (pShip->bRefueling != 0) { /* 0x7D000 */
                    PanelLineCopy(pStatus, nRow, PlaneTextRec(pEnv, PUFO_PLANE_STATUS_REFUELING));
                    UiPanel_SetLineMeta(pStatus, nRow, 250, 50, 50, 25, 10, 10); /* 0x7D060 */
                }
                if (pShip->uRangeRemain != pShip->uRangeMax && pShip->bRefueling == 0) { /* 0x7D098 */
                    PanelLineCopy(pStatus, nRow, PlaneTextRec(pEnv, PUFO_PLANE_STATUS_READY));
                    UiPanel_SetLineMeta(pStatus, nRow, 100, 200, 100, 10, 25, 10); /* 0x7D0F4 */
                }
                if (pShip->bRefueling == 0 && pShip->uRangeRemain == pShip->uRangeMax) { /* 0x7D114 */
                    PanelLineCopy(pStatus, nRow, PlaneTextRec(pEnv, PUFO_PLANE_STATUS_FULL));
                    UiPanel_SetLineMeta(pStatus, nRow, 250, 250, 250, 25, 10, 10); /* 0x7D170 */
                }
            }
            ++nRow; /* 0x7D1DC */
        }
    }
}

/* ------------------------------------------------- the hangar craft list's two clicks */

static i32 PlaneRowCraftIdx(const UiPanel *pPanel, i32 nRow)
{
    if (pPanel == NULL || nRow < 0 || nRow >= PUFO_PANEL_LINES) {
        return -1;
    }
    return pPanel->anLineAux[nRow];
}

i32 BasePlaneScreen_SelectCraft(const PlaneCraftSelectEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pCraftPanel == NULL || pEnv->pnSelCraftIdx == NULL
        || pEnv->pnSelectedCraftIdx == NULL) {
        return 0;
    }

    i32 nIdx = PlaneRowCraftIdx(pEnv->pCraftPanel, pEnv->pCraftPanel->nUnk3032); /* 0x7CBEC */

    pEnv->pnSelCraftIdx[0] = nIdx;      /* 0x7CBF8: nPlaneSelCraftIdx @ 0x161F04 */
    pEnv->pnSelectedCraftIdx[0] = nIdx; /* 0x7CC00: nSelectedCraftIdx @ 0x161AC4 */

    /* 0x7CC14 LDRB [R3,#Ship.bAtBase] - at base, the hangar switches to the detail view. The original
     * indexes apCraft with no bound at all; the port checks it. */
    if (pEnv->pbShowCraftDetail != NULL && pEnv->apCraft != NULL && nIdx >= 0
        && pEnv->apCraft[nIdx] != NULL && pEnv->apCraft[nIdx]->bAtBase != 0) {
        *pEnv->pbShowCraftDetail = 1; /* 0x7CC20 STRB */
    }
    return nIdx;
}

i32 BasePlaneScreen_SelectListed(const PlaneCraftSelectEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pStatusPanel == NULL || pEnv->pnSelCraftIdx == NULL
        || pEnv->pnSelectedCraftIdx == NULL) {
        return 0;
    }

    /* 0x7CC54..0x7CC68: panel B's cursor indexes panel A's anLineAux. The two panels are built with the
     * same line count and their cursors move together, which is why the original works. */
    i32 nIdx = PlaneRowCraftIdx(pEnv->pCraftPanel, pEnv->pStatusPanel->nUnk3032);

    pEnv->pnSelCraftIdx[0] = nIdx;
    pEnv->pnSelectedCraftIdx[0] = nIdx;
    return nIdx;
}

/* ------------------------------------------------- the hangar's object block */

/* g_aPlaneMainDesc @ 0xABCC8 and g_aPlaneWeaponDesc @ 0xABD48: 4 and 7 UiControlDesc records, printed
 * and verified by tools/gen_geoiinit_tables.py. They sit back to back and run straight into
 * aPlaneCraftTypeOrder @ 0xABE28. */
static const UiControlDesc g_aPlaneMainDesc[PUFO_PLANE_MAIN_CTLS] = {
    { 20, 100, "initg", "cinitg", "", 0 },
    { 197, 100, "initg", "cinitg", "", 0 },
    { 20, 200, "emptyb1", "cemptyb1", "", 0 },
    { 20, 240, "emptyb1", "cemptyb1", "", 0 }
};

static const UiControlDesc g_aPlaneWeaponDesc[PUFO_PLANE_WEAPON_CTLS] = {
    { 15, 92, "emptyb", "cemptyb", "", 0 },
    { 15, 0, "emptyb", "cemptyb", "", 0 },
    { 15, 0, "emptyb", "cemptyb", "", 0 },
    { 15, 0, "emptyb", "cemptyb", "", 0 },
    { 15, 0, "emptyb", "cemptyb", "", 0 },
    { 15, 0, "emptyb", "cemptyb", "", 0 },
    { 15, 0, "emptyb", "cemptyb", "", 0 }
};

/* The pad names, straight from the IDB's string refs: aBaseb001..005 @ 0xABEB8..0xABE88 and
 * aBaseb049/050/051/053/054/052 @ 0xABE7C..0xABE40. The last two are swapped relative to their numbers,
 * which is what the six PadImage_Load calls at 0x7DD30..0x7DD80 do. */
static const char *const g_aPlaneCraftPads[PUFO_PLANE_CRAFT_PADS] = {
    "baseb001", "baseb002", "baseb003", "baseb004", "baseb005"
};
static const char *const g_aPlaneWeaponPads[PUFO_PLANE_WEAPON_PADS] = {
    "baseb049", "baseb050", "baseb051", "baseb053", "baseb054", "baseb052"
};

#define PUFO_PLANE_BACKDROP_PAD "plane"  /* aPlane_0 @ 0xABEC4, loaded at mode 0 */
#define PUFO_PLANE_BACKDROP_MODE 0
#define PUFO_PLANE_ART_MODE 3            /* every craft and weapon pad loads at mode 3 */
#define PUFO_PLANE_CRAFT_CENTRE_X 120    /* nOriginX = 120 - (uWidth >> 1) */
#define PUFO_PLANE_CRAFT_Y 80
#define PUFO_PLANE_WEAPON_Y 95

static PadImage *PlanePadNew(void)
{
    /* deviation: the original's operator new 0x14 leaves uWidth / uHeight / nOriginX / nOriginY
     * uninitialised - PadImage_ctor @ 0x11000 only sets pData to null - and PadImage_Load does not touch
     * them when the load fails. The port zeroes the block, which only shows up on a failed load. */
    PadImage *pPad = (PadImage *)calloc(1, sizeof(PadImage)); /* operator new 0x14 */

    if (pPad == NULL) {
        return NULL;
    }
    PadImage_ctor(pPad); /* 0x7DA00 */
    return pPad;
}

static UiControl *PlaneCtlNew(void)
{
    UiControl *pCtl = (UiControl *)malloc(sizeof(UiControl)); /* operator new 0x20 */

    if (pCtl == NULL) {
        return NULL;
    }
    UiControl_ctor(pCtl); /* 0x7DA44 */
    return pCtl;
}

void BasePlaneScreen_Init(const BasePlaneScreenInitEnv *pEnv)
{
    if (pEnv == NULL || pEnv->pScreen == NULL) {
        return;
    }

    BasePlaneScreen *pScr = pEnv->pScreen;

    pScr->pBackdropPad = PlanePadNew(); /* 0x7D9F0..0x7DA14 */
    if (pScr->pBackdropPad != NULL) {
        (void)PadImage_Load(pScr->pBackdropPad, PUFO_PLANE_BACKDROP_PAD, PUFO_PLANE_BACKDROP_MODE);
        pScr->pBackdropPad->nOriginX = 0; /* 0x7DA28 */
        pScr->pBackdropPad->nOriginY = 0; /* 0x7DA30 */
    }

    for (i32 i = 0; i < PUFO_PLANE_MAIN_CTLS; ++i) {
        pScr->apMainCtl[i] = PlaneCtlNew(); /* 0x7DA38..0x7DA64 */
    }
    for (i32 i = 0; i < PUFO_PLANE_MAIN_CTLS; ++i) { /* 0x7DA7C..0x7DAA0 */
        if (pScr->apMainCtl[i] != NULL) {
            pScr->apMainCtl[i]->pfnClick = pEnv->apfnMainClick[i];
        }
    }
    for (i32 i = 0; i < PUFO_PLANE_MAIN_CTLS; ++i) { /* 0x7DAB4..0x7DAC8 */
        if (pScr->apMainCtl[i] != NULL) {
            UiControl_Setup(pScr->apMainCtl[i], 0, 0, &g_aPlaneMainDesc[i],
                            pEnv->pfnLoadPads, pEnv->pPadUser);
        }
    }

    for (i32 i = 0; i < PUFO_PLANE_WEAPON_CTLS; ++i) {
        pScr->apWeaponCtl[i] = PlaneCtlNew(); /* 0x7DAD8..0x7DB08 */
    }
    for (i32 i = 0; i < PUFO_PLANE_WEAPON_CTLS; ++i) { /* 0x7DB2C..0x7DB74 */
        if (pScr->apWeaponCtl[i] != NULL) {
            pScr->apWeaponCtl[i]->pfnClick = pEnv->apfnWeaponClick[i];
        }
    }
    for (i32 i = 0; i < PUFO_PLANE_WEAPON_CTLS; ++i) { /* 0x7DB88..0x7DB9C */
        if (pScr->apWeaponCtl[i] != NULL) {
            UiControl_Setup(pScr->apWeaponCtl[i], 0, 0, &g_aPlaneWeaponDesc[i],
                            pEnv->pfnLoadPads, pEnv->pPadUser);
        }
    }

    for (i32 i = 0; i < PUFO_PLANE_CRAFT_PADS; ++i) {
        pScr->apCraftPads[i] = PlanePadNew(); /* 0x7DBAC..0x7DBE8 */
    }
    for (i32 i = 0; i < PUFO_PLANE_CRAFT_PADS; ++i) { /* 0x7DC04..0x7DCDC */
        if (pScr->apCraftPads[i] == NULL) {
            continue;
        }
        (void)PadImage_Load(pScr->apCraftPads[i], g_aPlaneCraftPads[i], PUFO_PLANE_ART_MODE);
        pScr->apCraftPads[i]->nOriginX = PUFO_PLANE_CRAFT_CENTRE_X
                                       - ((i32)pScr->apCraftPads[i]->uWidth >> 1);
        pScr->apCraftPads[i]->nOriginY = PUFO_PLANE_CRAFT_Y;
    }

    for (i32 i = 0; i < PUFO_PLANE_WEAPON_PADS; ++i) {
        pScr->apWeaponPads[i] = PlanePadNew(); /* 0x7DCE4..0x7DD14 */
    }
    for (i32 i = 0; i < PUFO_PLANE_WEAPON_PADS; ++i) { /* 0x7DD30..0x7DD80 */
        if (pScr->apWeaponPads[i] == NULL) {
            continue;
        }
        (void)PadImage_Load(pScr->apWeaponPads[i], g_aPlaneWeaponPads[i], PUFO_PLANE_ART_MODE);
        pScr->apWeaponPads[i]->nOriginY = PUFO_PLANE_WEAPON_Y; /* 0x7DD98, the six slots at +0x18..+0x2C */
    }

    if (pEnv->pCloseCtl != NULL) {
        pEnv->pCloseCtl->x = 224;                    /* 0x7DDB4 */
        pEnv->pCloseCtl->y = 1;                      /* 0x7DDC0 */
        pEnv->pCloseCtl->pfnClick = pEnv->pfnCloseClick; /* 0x7DDCC */
    }
    if (pEnv->pBuySellCtl != NULL) {
        pEnv->pBuySellCtl->pfnClick = pEnv->pfnBuySellClick; /* 0x7DDDC */
    }

    BasePlaneScreen_BuildPanels(pEnv->pPanels, pEnv->pBuildEnv); /* 0x7DDE8, the tail call */
}

/* stru_ABC88 @ 0xABC88: the two nav controls BuildControls installs, in the order the UiControl_Setup
 * loop at 0x7B110 consumes them. The crew-list pair at 0xABC48 carries the same two names at y 20 and 40;
 * these are at y 18 and 38. */
static const UiControlDesc g_aPlaneStoreNavDesc[PUFO_PLANE_STORE_NAV] = {
    { 0x00CD, 0x0012, "up",   "sup",   "", 0 }, /* 0xABC88 */
    { 0x00CD, 0x0026, "down", "sdown", "", 0 }, /* 0xABCA8 */
};

/* 0x7B190..0x7B1A4: `CMP #0x19 / BLT keep`, `CMP #0x1E / BLE skip`, `CMP #0x34 / BGE skip`. Slots 0,
 * 25..30 and 52 up get no row -- 25..30 are the six weapon slots the hangar handles through its own
 * weapon columns instead (the SelectWeapon25..30 handlers), and 31..35 are the five crew slots its
 * crew quota sums. */
static bool PlaneStoreSlotAdmits(i32 nSlot)
{
    return nSlot < 25 || (nSlot > 30 && nSlot < 52);
}

/* 0x7B1D4..0x7B1F0: byte_AA968[slot] names the research topic that unlocks the slot, and the gate is that
 * topic's bUnlocked at ResearchTopicLive + 0xD. */
static bool PlaneStoreSlotUnlocked(const PlaneStoreBuildEnv *pEnv, i32 nSlot)
{
    if (pEnv->abSlotTopic == NULL || pEnv->aTopics == NULL
        || nSlot < 0 || nSlot >= PUFO_PLANE_SLOT_TOPIC_COUNT) {
        return false;
    }
    const i32 nTopic = pEnv->abSlotTopic[nSlot];

    if (nTopic < 0 || nTopic >= PUFO_RESEARCH_TOPIC_COUNT) {
        return false; /* deviation: the original indexes the topic table with no bound */
    }
    return pEnv->aTopics[nTopic].bUnlocked != 0;
}

static const ObjectRecFull *PlaneStoreObject(const PlaneStoreBuildEnv *pEnv, i32 nSlot)
{
    if (pEnv->apObjects == NULL || nSlot < 0 || nSlot >= pEnv->nObjectCount) {
        return NULL; /* deviation: the original indexes the array whatever the slot says */
    }
    return pEnv->apObjects[nSlot];
}

/* auPerBaseVal @ ObjectRecFull + 0x2A (0x7B1B8 LDRH) and aItemQty @ + 0x3A (0x7B1C8 LDRH). Both are
 * bounded here; the original uses the signed base byte and the craft dword index straight as subscripts. */
static u16 PlaneStoreBaseQty(const PlaneStoreBuildEnv *pEnv, const ObjectRecFull *pObj)
{
    if (pObj == NULL || pEnv->nBaseIdx < 0 || pEnv->nBaseIdx >= PUFO_FAC_BASE_COUNT) {
        return 0;
    }
    return pObj->auPerBaseVal[pEnv->nBaseIdx];
}

static u16 PlaneStoreCraftQty(const PlaneStoreBuildEnv *pEnv, const ObjectRecFull *pObj)
{
    if (pObj == NULL || pEnv->nCraftIdx < 0 || pEnv->nCraftIdx >= PUFO_PLANE_CRAFT_QTY_SLOTS) {
        return 0;
    }
    return pObj->aItemQty[pEnv->nCraftIdx];
}

/* The five store panels share nStyle 14, nFont 16, nPosY 70, nRectY 70 and both colour triples, and
 * differ only as the header table records. nRightAlign marks the two count columns (avail, taken), which
 * clear bSelLineHighlight, set bAuxNumbers and get nUnk7076 2; nRowExtras marks the two 15-wide quantity columns,
 * which add aStyleRgb[3..5] 32,32,FA and the nRepeatDelay 5 auto-repeat. */
static void PlaneStorePanelStyle(UiPanel *pPanel, i32 nPosX, i32 nRectX, i32 nViewW,
                                 bool bRowExtras, bool bRightAlign)
{
    pPanel->nStyle = 14;
    pPanel->nFont = 16;
    pPanel->nPosY = 70;
    pPanel->nRectX = nRectX;
    pPanel->nRectY = 70;
    pPanel->nPosX = nPosX;
    pPanel->nViewW = nViewW;
    UiPanel_SetBytes0to2(pPanel, 0xB4, 0xB4, 0xB4);
    UiPanel_SetBytes6to11(pPanel, 0xF0, 0xF0, 0xF0, 0x14, 0x14, 0x14);

    if (bRightAlign) {
        pPanel->bSelLineHighlight = 0;
        pPanel->bAuxNumbers = 1;
        pPanel->nUnk7076 = 2;
        return;
    }
    pPanel->bSelLineHighlight = 1;
    pPanel->bColorKey = 0x96;

    if (bRowExtras) {
        UiPanel_SetBytes3to5(pPanel, 0x32, 0x32, 0xFA);
        pPanel->nRepeatDelay = 5;
    }
}

/* BasePlaneScreen_BuildControls @ 0x7b014. See the header for the branch map and the panel table. */
void BasePlaneScreen_BuildControls(PlaneStorePanels *pOut, const PlaneStoreBuildEnv *pEnv)
{
    i32 anBaseQty[PUFO_PLANE_SCAN_SLOTS];
    i32 anCarryQty[PUFO_PLANE_SCAN_SLOTS];
    UiPanel *apFree[5];
    char szName[112]; /* the original's v67 @ 0x7B018's frame slot */
    i32 nRows = 0;
    i32 nQuota = 0;
    i32 nSlots;
    i32 nRow;
    i32 i;

    if (pOut == NULL || pEnv == NULL || pEnv->pbStoresCommitted == NULL) {
        return;
    }

    if (*pEnv->pbStoresCommitted != 0) {
        *pEnv->pbStoresCommitted = 0; /* 0x7BA18: the flag goes down before anything is released */

        for (i = 0; i < PUFO_PLANE_STORE_NAV; ++i) {
            PlaneFreeCtl(&pOut->apNavCtl[i]); /* 0x7BA1C..0x7BA44, the object block's +0x64 / +0x68 */
        }
        apFree[0] = pOut->pSlotPanel;   /* 0x7BA48 */
        apFree[1] = pOut->pSelRowPanel; /* 0x7BA68 */
        apFree[2] = pOut->pAvailPanel;
        apFree[3] = pOut->pCommitPanel;
        apFree[4] = pOut->pTakenPanel;
        for (i = 0; i < 5; ++i) {
            PlanePanelFree(apFree[i]); /* 0x7BA5C..0x7BAE4 */
        }
        pOut->pSlotPanel = NULL;   /* deviation: the original leaves all seven slots dangling */
        pOut->pSelRowPanel = NULL;
        pOut->pAvailPanel = NULL;
        pOut->pCommitPanel = NULL;
        pOut->pTakenPanel = NULL;
        return;
    }

    if (pEnv->anSlotCarryCount != NULL) {
        for (i = 0; i < PUFO_PLANE_CARRY_SLOTS; ++i) {
            pEnv->anSlotCarryCount[i] = 0; /* 0x7B038..0x7B058: 256 stores */
        }
    }

    /* 0x7B05C..0x7B09C: the crew quota starts as what the craft already carries of the five crew slots.
     * The original stores the running total on every step; the port writes it once, which is the same
     * value and the only value any reader can see. */
    for (i = PUFO_PLANE_CREW_SLOT_FIRST; i <= PUFO_PLANE_CREW_SLOT_LAST; ++i) {
        nQuota += (i32)PlaneStoreCraftQty(pEnv, PlaneStoreObject(pEnv, i));
    }
    if (pEnv->pnCrewQuota != NULL) {
        *pEnv->pnCrewQuota = nQuota;
    }

    for (i = 0; i < PUFO_PLANE_STORE_NAV; ++i) {
        UiControl *pCtl = (UiControl *)malloc(sizeof(UiControl)); /* 0x7B0AC: operator new 0x20 */

        if (pCtl != NULL) {
            UiControl_ctor(pCtl); /* 0x7B0BC */
        }
        pOut->apNavCtl[i] = pCtl;
    }
    for (i = 0; i < PUFO_PLANE_STORE_NAV; ++i) {
        if (pOut->apNavCtl[i] != NULL) {
            pOut->apNavCtl[i]->pfnClick = pEnv->apfnNavClick[i]; /* 0x7B0FC / 0x7B10C, before Setup */
        }
    }
    for (i = 0; i < PUFO_PLANE_STORE_NAV; ++i) {
        if (pOut->apNavCtl[i] != NULL) {
            UiControl_Setup(pOut->apNavCtl[i], 0, 0, &g_aPlaneStoreNavDesc[i],
                            pEnv->pfnLoadPads, pEnv->pPadUser); /* 0x7B110..0x7B128 */
        }
    }

    *pEnv->pbStoresCommitted = 1; /* 0x7B12C: set before a single row exists */

    nSlots = pEnv->nObjectCount;
    if (nSlots > PUFO_PLANE_SCAN_SLOTS + 1) {
        nSlots = PUFO_PLANE_SCAN_SLOTS + 1; /* deviation: the 2032-byte scan block is unbounded */
    }

    /* The scan, 0x7B130..0x7B22C. The pair is cleared before any test runs, so a refused slot leaves a
     * zero pair the row pass can re-test rather than an uninitialised one. */
    for (i = 1; i < nSlots; ++i) {
        const ObjectRecFull *pObj = PlaneStoreObject(pEnv, i);
        const u16 wBase = PlaneStoreBaseQty(pEnv, pObj);
        const u16 wCarry = PlaneStoreCraftQty(pEnv, pObj);

        anBaseQty[i - 1] = 0;  /* 0x7B180 */
        anCarryQty[i - 1] = 0; /* 0x7B188 */

        if (!PlaneStoreSlotAdmits(i) || (wBase == 0 && wCarry == 0) || !PlaneStoreSlotUnlocked(pEnv, i)) {
            continue;
        }
        anBaseQty[i - 1] = (i32)wBase;  /* 0x7B1F0 STRNE */
        anCarryQty[i - 1] = (i32)wCarry; /* 0x7B1FC STRNE */
        ++nRows;                         /* 0x7B204 */
    }

    pOut->pSlotPanel   = PlanePanelNew(nRows); /* 0x7B218 */
    pOut->pAvailPanel  = PlanePanelNew(nRows); /* 0x7B310 */
    pOut->pSelRowPanel = PlanePanelNew(nRows); /* 0x7B418 */
    pOut->pCommitPanel = PlanePanelNew(nRows); /* 0x7B538 */
    pOut->pTakenPanel  = PlanePanelNew(nRows); /* 0x7B65C */

    if (pOut->pSlotPanel != NULL) {
        PlaneStorePanelStyle(pOut->pSlotPanel, 10, 5, 125, false, false);
        pOut->pSlotPanel->pfnClick = pEnv->pfnSlotClick; /* 0x7B244 */
    }
    if (pOut->pAvailPanel != NULL) {
        PlaneStorePanelStyle(pOut->pAvailPanel, 164, 130, 50, false, true);
        pOut->pAvailPanel->pfnClick = NULL; /* 0x7B354 */
    }
    if (pOut->pSelRowPanel != NULL) {
        PlaneStorePanelStyle(pOut->pSelRowPanel, 170, 166, 15, true, false);
        pOut->pSelRowPanel->pfnClick = pEnv->pfnSelRowClick; /* 0x7B454 */
    }
    if (pOut->pCommitPanel != NULL) {
        PlaneStorePanelStyle(pOut->pCommitPanel, 186, 182, 15, true, false);
        pOut->pCommitPanel->pfnClick = pEnv->pfnCommitClick; /* 0x7B574 */
    }
    if (pOut->pTakenPanel != NULL) {
        PlaneStorePanelStyle(pOut->pTakenPanel, 232, 190, 45, false, true);
        pOut->pTakenPanel->pfnClick = NULL; /* 0x7B6C4 */
    }

    if (pOut->pSlotPanel == NULL || pOut->pAvailPanel == NULL || pOut->pSelRowPanel == NULL
        || pOut->pCommitPanel == NULL || pOut->pTakenPanel == NULL) {
        return; /* deviation: the original writes v22->nStyle straight after a ctor that may fail */
    }

    /* The row pass, 0x7B7D0..0x7B930. The row index advances only for kept slots while the slot counter
     * advances for all of them, which is why the aux column holds a slot id and not a row index. */
    nRow = 0;
    for (i = 1; i < nSlots; ++i) {
        if (anBaseQty[i - 1] == 0 && anCarryQty[i - 1] == 0) {
            continue; /* 0x7B7F8: the stored pair is re-tested */
        }
        const ObjectRecFull *pObj = PlaneStoreObject(pEnv, i);

        snprintf(szName, sizeof szName, "%s", (pObj != NULL) ? pObj->szName : "");
        if (strlen(szName) > 17) {
            szName[17] = 0; /* 0x7B838: strlen above 0x11, and index 17 is the terminator */
        }
        (void)UiPanel_SetLineText(pOut->pSlotPanel, nRow, szName);                  /* 0x7B854..0x7B878 */
        UiPanel_SetLineAux(pOut->pSlotPanel, nRow, i);                              /* 0x7B8A0 */
        (void)UiPanel_SetLineText(pOut->pSelRowPanel, nRow, PUFO_PLANE_ROW_DEC);    /* 0x7B8C0..0x7B8E8 */
        (void)UiPanel_SetLineText(pOut->pCommitPanel, nRow, PUFO_PLANE_ROW_INC);    /* 0x7B8F8..0x7B920 */
        UiPanel_SetLineAux(pOut->pAvailPanel, nRow, anBaseQty[i - 1]);  /* 0x7B918: anLineAux[row] @ 0x13B4 */
        UiPanel_SetLineAux(pOut->pTakenPanel, nRow, anCarryQty[i - 1]); /* 0x7B924 */
        ++nRow;
    }

    /* The sort, 0x7B950..0x7B9F8: the first nObjectCount - 1 order entries, three panels swapped together
     * rather than the buy/sell builders' five. */
    {
        i32 nPlaced = 0;
        i32 nOrder = (nSlots > 0) ? nSlots - 1 : 0;

        if (nOrder > PUFO_BUYSELL_ORDER_COUNT) {
            nOrder = PUFO_BUYSELL_ORDER_COUNT;
        }
        for (i = 0; i < nOrder; ++i) {
            const i32 nWant = g_aBuySellRowOrder[i];
            i32 r;

            for (r = 0; r < pOut->pSlotPanel->nLineCount; ++r) { /* 0x7B9D8: the panel's own line count */
                if (pOut->pSlotPanel->anLineAux[r] == nWant) {
                    (void)UiPanel_SwapLines(pOut->pSlotPanel, nPlaced, r);  /* 0x7B97C */
                    (void)UiPanel_SwapLines(pOut->pAvailPanel, nPlaced, r); /* 0x7B98C */
                    (void)UiPanel_SwapLines(pOut->pTakenPanel, nPlaced, r); /* 0x7B99C */
                    ++nPlaced;
                }
            }
        }
    }
}

/* --- BasePlaneScreen_Draw @ 0x7d374: the hangar frame, the craft readout and the rails.
 * The header carries the gate map and the rail geometry. */

/* One record lookup for both hangar draws: the table is aTextRecs @ 0xB38D0 with the memory stride. */
static const char *PlaneRecText(const char *pTextRecs, i32 nRec)
{
    return (pTextRecs != NULL) ? pTextRecs + PUFO_TEXT_REC_STRIDE * nRec : "";
}

/* 0x7D3D0..0x7D414: the readout runs only with the preview flag set and the pointer row inside the
 * status panel's band -- 16 pixels per row, and no nStyle cap here, unlike the transfer frame's. */
static bool PlaneDrawPreviewArmed(const PlaneDrawEnv *pEnv)
{
    i32 nTop;
    i32 nBand;

    if (pEnv->pbPreviewActive == NULL || *pEnv->pbPreviewActive == 0 || pEnv->pnCursorY == NULL
        || pEnv->pStatusPanel == NULL) {
        return false;
    }
    nTop = pEnv->pStatusPanel->nPosY;                    /* +0x1B88, read at 0x7D3E0 */
    nBand = nTop + 16 * pEnv->pStatusPanel->nLineCount;  /* +0xBDC, 0x7D408 ADD R3,R2,R3,LSL#4 */
    return *pEnv->pnCursorY > nTop && *pEnv->pnCursorY < nBand; /* 0x7D3F4 BLE, 0x7D414 BGE */
}

/* One of the two slot sums. The original carries them as two loops of one shape (0x7D448..0x7D478 and
 * 0x7D490..0x7D4C0): load apObjects[i]->aItemQty[craft], skip the slot when it is zero, then the two
 * band compares. */
static i32 PlaneDrawSumSlots(const PlaneDrawEnv *pEnv, i32 nCraftIdx, i32 nSlotFirst, i32 nSlotLast)
{
    i32 nSum = 0;

    for (i32 i = 0; i < pEnv->nObjectCount; ++i) {
        const ObjectRecFull *pObj = (pEnv->apObjects != NULL) ? pEnv->apObjects[i] : NULL;
        u16 wQty;

        if (pObj == NULL) {
            continue;
        }
        wQty = (nCraftIdx >= 0 && nCraftIdx < PUFO_PLANE_CRAFT_QTY_SLOTS)
                 ? pObj->aItemQty[nCraftIdx] : 0u; /* 0x7D448 LDRH [obj,#0x3A], unbounded there */
        if (wQty == 0) {
            continue; /* 0x7D454 / 0x7D4A0 */
        }
        if (i < nSlotFirst || i > nSlotLast) {
            continue; /* 0x7D45C / 0x7D464, 0x7D4A4 / 0x7D4AC */
        }
        nSum += wQty;
    }
    return nSum;
}

/* 0x7D4C4..0x7D500: the soldiers assigned to this craft. ARM compares with `CMP R3,R5` where R3 is the
 * zero-extended byte and R5 the dword index, so a craft index above 255 matches nothing -- the port
 * keeps that by comparing the promoted byte against the index. */
static i32 PlaneDrawCountAboard(const PlaneDrawEnv *pEnv, i32 nCraftIdx)
{
    i32 nCount = 0;

    for (i32 i = 0; i < pEnv->nSoldierCount; ++i) {
        const Soldier *pSol = (pEnv->apSoldiers != NULL) ? pEnv->apSoldiers[i] : NULL;

        if (pSol != NULL && (i32)pSol->bUnk183 == nCraftIdx) { /* 0x7D4F0 LDRB [sol,#0xB7] */
            ++nCount;
        }
    }
    return nCount;
}

/* Rail C, 0x7D840..0x7D8FC: the inner frame's top and bottom edges. A climbing x from 15 against a
 * descending x from 220, both stepping 5 while the descending one stays above 115 -- 21 steps. */
static void PlaneDrawInnerRailsH(void)
{
    for (i32 n = 0, x1 = PUFO_PLANE_INNER_X_FIRST, x2 = PUFO_PLANE_INNER_X_DESCEND;
         x2 > PUFO_PLANE_INNER_X_STOP; x1 += PUFO_FRAME_RAMP_STEP, x2 -= PUFO_FRAME_RAMP_STEP, ++n) {
        const u8 nRise = (u8)(PUFO_FRAME_RAMP_STEP * (n + PUFO_FRAME_RAMP_BASE)); /* 100..200 */

        SoftSurf_BlitRectProxy(0, x1, PUFO_PLANE_INNER_Y_TOP, 5, 1, nRise, nRise, nRise);
        SoftSurf_BlitRectProxy(0, x1, PUFO_PLANE_INNER_Y_BOTTOM, 5, 1, nRise, nRise, nRise);
        SoftSurf_BlitRectProxy(0, x2, PUFO_PLANE_INNER_Y_TOP, 5, 1, nRise, nRise, nRise);
        SoftSurf_BlitRectProxy(0, x2, PUFO_PLANE_INNER_Y_BOTTOM, 5, 1, nRise, nRise, nRise);
    }
}

/* Rail D, 0x7D8FC..0x7D9B4: the inner frame's left and right edges. The bound is tested after the step, so
 * the body that lands on m = -34 (v = 17) still draws -- 18 bodies, colour 100..185 -- and the climbing row
 * 2*v+15 meets the descending row m+83 at y 49..50 rather than leaving a gap. */
static void PlaneDrawInnerRailsV(void)
{
    for (i32 m = 0, v = 0; m > PUFO_PLANE_INNER_D_STOP; m -= 2, ++v) {
        const u8 nRise = (u8)(PUFO_FRAME_RAMP_STEP * (v + PUFO_FRAME_RAMP_BASE)); /* 100..180 */

        SoftSurf_BlitRectProxy(0, PUFO_PLANE_INNER_X_FIRST, 2 * v + PUFO_PLANE_INNER_Y_TOP, 1, 2,
                               nRise, nRise, nRise);
        SoftSurf_BlitRectProxy(0, PUFO_PLANE_INNER_X_FIRST, m + PUFO_PLANE_INNER_D_Y_BASE, 1, 2,
                               nRise, nRise, nRise);
        SoftSurf_BlitRectProxy(0, PUFO_PLANE_INNER_X_RIGHT, 2 * v + PUFO_PLANE_INNER_Y_TOP, 1, 2,
                               nRise, nRise, nRise);
        SoftSurf_BlitRectProxy(0, PUFO_PLANE_INNER_X_RIGHT, m + PUFO_PLANE_INNER_D_Y_BASE, 1, 2,
                               nRise, nRise, nRise);
    }
}

i32 BasePlaneScreen_Draw(const PlaneDrawEnv *pEnv)
{
    char szNum[PUFO_PLANE_ITOA_BUF];  /* var_34 @ sp+0x48 */
    char szVal[PUFO_PLANE_VALUE_BUF]; /* psz @ sp+0x10 */
    const Ship *pCraft;
    i32 nCraftIdx = 0;
    i32 nArmament;
    i32 nCrewQty;
    i32 nAboard;

    if (pEnv == NULL) {
        return 0;
    }

    /* 0x7D37C: the detail view owns the screen, and its value is this function's value. */
    if (pEnv->pbShowCraftDetail != NULL && *pEnv->pbShowCraftDetail != 0) {
        /* 0x7D37C: the whole call goes to the detail screen and its value is returned. */
        return BasePlaneScreen_DrawCraftDetail(pEnv->pDetail);
    }

    if (pEnv->pbPanelsBuilt != NULL && *pEnv->pbPanelsBuilt != 0) {
        if (pEnv->pCraftPanel != NULL) {
            UiPanel_DrawText(pEnv->pCraftPanel); /* 0x7D3B4 */
        }
        if (pEnv->pStatusPanel != NULL) {
            UiPanel_DrawText(pEnv->pStatusPanel); /* 0x7D3C4 */
        }

        if (PlaneDrawPreviewArmed(pEnv)) {
            nCraftIdx = (pEnv->pnSelCraftIdx != NULL) ? *pEnv->pnSelCraftIdx : 0;
            nArmament = PlaneDrawSumSlots(pEnv, nCraftIdx, PUFO_PLANE_WEAPON_SLOT_FIRST,
                                          PUFO_PLANE_WEAPON_SLOT_LAST);
            nCrewQty = PlaneDrawSumSlots(pEnv, nCraftIdx, PUFO_PLANE_CREW_SLOT_FIRST,
                                         PUFO_PLANE_CREW_SLOT_LAST);
            nAboard = PlaneDrawCountAboard(pEnv, nCraftIdx);

            (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 250, 250, 20, 25, 20); /* 0x7D504 */
            Text_DrawString(25, 23, PlaneRecText(pEnv->pTextRecs, PUFO_PLANE_TEXT_ARMAMENT));   /* 0x7D540 */
            Text_DrawString(25, 43, PlaneRecText(pEnv->pTextRecs, PUFO_PLANE_TEXT_CREW));       /* 0x7D55C */
            Text_DrawString(25, 63, PlaneRecText(pEnv->pTextRecs, PUFO_PLANE_TEXT_CREW_QTY));   /* 0x7D578 */

            /* 0x7D588..0x7D5E0: psz is itoa(v3), then strcat with the backslash at aQtyPairSep, then
             * strcat with itoa(apCraft[nPlaneSelCraftIdx]->bCrewSlots). The port builds the same string
             * in one bounded snprintf instead of three unbounded writes into the 56-byte psz. */
            pCraft = (pEnv->apCraft != NULL && nCraftIdx >= 0 && nCraftIdx < pEnv->nCraftCount)
                       ? pEnv->apCraft[nCraftIdx] : NULL;
            (void)snprintf(szVal, sizeof(szVal), "%d%s%u", nArmament, PUFO_PLANE_QTY_SEP,
                           (unsigned)((pCraft != NULL) ? pCraft->bCrewSlots : 0u));
            Text_DrawString(190, 23, szVal); /* 0x7D5F4 */

            (void)snprintf(szNum, sizeof(szNum), "%d", nAboard);
            Text_DrawString(195, 43, szNum); /* 0x7D618 */

            (void)snprintf(szNum, sizeof(szNum), "%d", nCrewQty);
            Text_DrawString(195, 63, szNum); /* 0x7D63C */
        }
    }

    /* 0x7D640: both skipped paths land here, so the two captions and all four rail loops run whatever
     * the two flags say. The rails come AFTER the text, so they paint over it where they overlap. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 250, 250, 10, 10, 25);
    Text_DrawString(25, 90, PlaneRecText(pEnv->pTextRecs, PUFO_PLANE_TEXT_CRAFT_CAPTION));   /* 0x7D67C */
    Text_DrawString(165, 90, PlaneRecText(pEnv->pTextRecs, PUFO_PLANE_TEXT_STATUS_CAPTION)); /* 0x7D698 */

    BaseUi_DrawFrameRails(); /* 0x7D6AC..0x7D840, the same rails the transfer frame draws */
    PlaneDrawInnerRailsH();  /* 0x7D840..0x7D8FC */
    PlaneDrawInnerRailsV();  /* 0x7D8FC..0x7D9B4 */
    return 0;
}

/* ------------------------------------------------- the hangar's other face
 *
 * The header carries the gate map, the two arms and the deviation list. */

static void PlaneDetailDrawCentered(i32 nCentre, i32 y, const char *psz)
{
    const i32 nWidth = Text_MeasureWidth(psz); /* the ARM keeps the width in R5 for the draw */
    Text_DrawString(nCentre - nWidth / 2, y, psz);
}

/* 0x7BEC8..0x7BF50: the four column separators, 1 wide and band + 2 tall, all in palette index 0xB4. */
static void PlaneDetailSeparator(i32 x, i32 nHeight)
{
    SoftSurf_BlitRectProxy(0, x, PUFO_PLANE_SEP_Y, 1, nHeight,
                           PUFO_PLANE_SEP_COLOR, PUFO_PLANE_SEP_COLOR, PUFO_PLANE_SEP_COLOR);
}

/* 0x7C4EC..0x7C488 and 0x7C5A8..0x7C544: the six mount arms pick a pad by weapon id, and the pad order
 * is not the weapon order -- weapon 25 takes the third pad, 26 the first, 27 the second. */
static const i8 s_anPlaneMountPad[6] = { 2, 0, 1, 3, 4, 5 };

static void PlaneDetailBlitMount(const PlaneDetailEnv *pEnv, i32 nWeapon, i32 nOriginX)
{
    if (pEnv->pScreen == NULL || nWeapon < PUFO_PLANE_WEAPON_SLOT_FIRST
        || nWeapon > PUFO_PLANE_WEAPON_SLOT_LAST) {
        return; /* 0x7C484 / 0x7C540: a weapon byte outside 25..30 blits nothing */
    }

    PadImage *pPad = pEnv->pScreen->apWeaponPads[s_anPlaneMountPad[nWeapon - PUFO_PLANE_WEAPON_SLOT_FIRST]];
    if (pPad == NULL) {
        return;
    }

    pPad->nOriginX = nOriginX; /* 0x7C490 / 0x7C54C: the mount column is written into the pad first */
    PadImage_BlitEx(pPad, PUFO_PLANE_PAD_KEY);
}

/* 0x7C5C0..0x7C68C: the soldier walk fills three counters in one pass. The last two are not drawn
 * here -- BasePlaneScreen_BuildListPanels sizes the crew panel with aboard + unassigned. */
static void PlaneDetailCountSoldiers(const PlaneDetailEnv *pEnv, i32 nCraftIdx, i32 nBaseIdx)
{
    i32 nAboard = 0;
    i32 nAtBase = 0;
    i32 nUnassigned = 0;

    for (i32 i = 0; i < pEnv->nSoldierCount; ++i) { /* 0x7C614: a non-positive count skips the walk */
        const Soldier *pSol = (pEnv->apSoldiers != NULL) ? pEnv->apSoldiers[i] : NULL;
        if (pSol == NULL) {
            continue;
        }
        if ((i32)pSol->bUnk183 == nCraftIdx) { /* 0x7C650, the byte widened against the dword index */
            ++nAboard;
        }
        if ((i32)pSol->nKind == nBaseIdx) { /* 0x7C664 and 0x7C674: Soldier.nKind @ +0xB6, twice */
            ++nAtBase;
            if (pSol->bUnk183 == PUFO_SOLDIER_CRAFT_NONE) { /* 0x7C678..0x7C680 */
                ++nUnassigned;
            }
        }
    }

    if (pEnv->pnSoldiersAboard != NULL) {
        *pEnv->pnSoldiersAboard = nAboard;
    }
    if (pEnv->pnSoldiersAtBase != NULL) {
        *pEnv->pnSoldiersAtBase = nAtBase;
    }
    if (pEnv->pnSoldiersUnassigned != NULL) {
        *pEnv->pnSoldiersUnassigned = nUnassigned;
    }
}

/* 0x7BFB8..0x7BFE0: the hovered row's object, reached through the slot panel's aux column. */
static const ObjectRecFull *PlaneDetailHoveredObject(const PlaneDetailEnv *pEnv)
{
    const i32 nRow = (pEnv->pnRowCursor != NULL) ? *pEnv->pnRowCursor : 0;

    if (pEnv->pSlotPanel == NULL || pEnv->apObjects == NULL
        || nRow < 0 || nRow >= PUFO_PANEL_LINES) {
        return NULL;
    }

    const i32 nSlot = pEnv->pSlotPanel->anLineAux[nRow];
    if (nSlot < 0 || nSlot >= pEnv->nObjectCount) {
        return NULL;
    }

    return pEnv->apObjects[nSlot];
}

i32 BasePlaneScreen_DrawCraftDetail(const PlaneDetailEnv *pEnv)
{
    char szName[PUFO_PLANE_DETAIL_NAME_MAX];    /* psz @ sp+0x88 */
    char szScratch[PUFO_PLANE_DETAIL_SCRATCH];  /* var_A4 @ sp+0x18 */
    char szNum[PUFO_PLANE_DETAIL_ITOA];         /* var_6C @ sp+0x50 */

    if (pEnv == NULL) {
        return 0;
    }

    BaseUi_DrawFrameRails(); /* 0x7BAF8..0x7BCA8: byte-for-byte the shared screen rails */

    const i32 nCraftIdx = (pEnv->pnCraftIdx != NULL) ? *pEnv->pnCraftIdx : -1;
    const i32 nBaseIdx = (pEnv->pnBaseIdx != NULL) ? (i32)*pEnv->pnBaseIdx : -1;
    Ship *pCraft = (pEnv->apCraft != NULL && nCraftIdx >= 0 && nCraftIdx < pEnv->nCraftCount)
                       ? pEnv->apCraft[nCraftIdx] : NULL;

    /* 0x7BCAC..0x7BCE4: the display name, and its width, which four separate exits reuse. */
    (void)Ship_FormatDisplayName(pEnv->apCraft, nCraftIdx, (const u8 *)pEnv->pTextRecs,
                                 szName, sizeof(szName));

    if (pEnv->pbListPanelsBuilt != NULL && *pEnv->pbListPanelsBuilt != 0) {
        /* 0x7BCF8..0x7BD44: the crew pair, the name at y 30, and a return. Both the stores panel set
         * and the craft read-out are skipped. */
        if (pEnv->pCrewPanel != NULL) {
            UiPanel_DrawText(pEnv->pCrewPanel); /* 0x7BD00 */
        }
        if (pEnv->pCrewStatusPanel != NULL) {
            UiPanel_DrawText(pEnv->pCrewStatusPanel); /* 0x7BD0C */
        }
        (void)Gfx_PackRgb565Pair(pEnv->pColor, 200, 200, 200, 10, 25, 10); /* 0x7BD34 */
        PlaneDetailDrawCentered(120, 30, szName);
        return 0;
    }

    if (pEnv->pbStoresCommitted != NULL && *pEnv->pbStoresCommitted != 0) {
        /* ---- the stores arm, 0x7BD70..0x7C228 ---- */
        const u32 nFail = (pEnv->pdwActionFail != NULL) ? *pEnv->pdwActionFail : 0u;
        const u8 bRowFlag = (nFail == 1u) ? 0u : 1u; /* 0x7BDA4 clears, 0x7BDB8 sets */

        if (pEnv->pSelRowPanel != NULL) {
            pEnv->pSelRowPanel->bSelLineHighlight = bRowFlag;
        }
        if (pEnv->pCommitPanel != NULL) {
            pEnv->pCommitPanel->bSelLineHighlight = bRowFlag;
        }

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 150, 150, 255, 20, 20, 20); /* 0x7BDD0 */
        PlaneDetailDrawCentered(210, 54, PlaneRecText(pEnv->pTextRecs, PUFO_PLANE_TEXT_CRAFT)); /* 0x7BE08 */
        PlaneDetailDrawCentered(147, 54, PlaneRecText(pEnv->pTextRecs, PUFO_PLANE_TEXT_BASE));   /* 0x7BE3C */

        if (pEnv->pbPanelFlagScratch != NULL) {
            const u8 bFlag = *pEnv->pbPanelFlagScratch;
            if (pEnv->pSelRowPanel != NULL) {
                pEnv->pSelRowPanel->bRepeatArmed = bFlag; /* 0x7BE58 */
            }
            if (pEnv->pCommitPanel != NULL) {
                pEnv->pCommitPanel->bRepeatArmed = bFlag; /* 0x7BE68 */
            }
            *pEnv->pbPanelFlagScratch = 0; /* 0x7BE70: the flag is consumed here */
        }

        if (pEnv->pSlotPanel != NULL) {
            UiPanel_DrawText(pEnv->pSlotPanel); /* 0x7BE78 */
        }

        /* 0x7BE7C..0x7BEAC: the band is 16 * min(nLineCount, nStyle) of the SELECT-ROW panel. */
        i32 nRows = 0;
        if (pEnv->pSelRowPanel != NULL) {
            nRows = (pEnv->pSelRowPanel->nLineCount < pEnv->pSelRowPanel->nStyle)
                        ? pEnv->pSelRowPanel->nLineCount : pEnv->pSelRowPanel->nStyle;
        }
        const i32 nBand = nRows * PUFO_PLANE_ROW_LINE_H;
        const i32 nSepH = nBand + PUFO_PLANE_ROW_BAND_PAD;

        PlaneDetailSeparator(PUFO_PLANE_SEP_X_FIRST, nSepH); /* 0x7BEC8 */
        if (pEnv->pAvailPanel != NULL) {
            UiPanel_DrawText(pEnv->pAvailPanel); /* 0x7BED4 */
        }
        PlaneDetailSeparator(PUFO_PLANE_SEP_X_SECOND, nSepH); /* 0x7BEF8 */
        if (pEnv->pSelRowPanel != NULL) {
            UiPanel_DrawText(pEnv->pSelRowPanel); /* 0x7BF00 */
        }
        PlaneDetailSeparator(PUFO_PLANE_SEP_X_THIRD, nSepH); /* 0x7BF24 */
        if (pEnv->pCommitPanel != NULL) {
            UiPanel_DrawText(pEnv->pCommitPanel); /* 0x7BF2C */
        }
        PlaneDetailSeparator(PUFO_PLANE_SEP_X_FOURTH, nSepH); /* 0x7BF50 */
        if (pEnv->pTakenPanel != NULL) {
            UiPanel_DrawText(pEnv->pTakenPanel); /* 0x7BF5C */
        }

        /* 0x7BF60..0x7BF90: bBaseFacPreviewOk, not the bBaseFacPreviewActive the read-out uses. */
        const i32 nCursorY = (pEnv->pnCursorY != NULL) ? *pEnv->pnCursorY : 0;
        const bool bArmed = (pEnv->pbPreviewOk != NULL && *pEnv->pbPreviewOk != 0)
                            && nCursorY > PUFO_PLANE_BAND_Y
                            && nCursorY < nBand + PUFO_PLANE_BAND_Y;

        if (bArmed) {
            (void)Gfx_PackRgb565Pair(pEnv->pColor, 150, 150, 255, 20, 20, 55); /* 0x7BFB4 */
            const ObjectRecFull *pObj = PlaneDetailHoveredObject(pEnv);
            if (pObj != NULL) {
                /* 0x7C0B4: the record pointer itself is the string -- szName is its first field. */
                Text_DrawString(10, PUFO_PLANE_BOTTOM_Y, pObj->szName);
            }
        } else {
            /* 0x7C014..0x7C040: the confirm control recentred onto the bottom row and blitted. */
            if (pEnv->pCtlConfirm != NULL) {
                pEnv->pCtlConfirm->x = (i16)(120 - (i32)(pEnv->pCtlConfirm->uWidth >> 1)); /* 0x7C01C */
                pEnv->pCtlConfirm->y = (i16)PUFO_PLANE_BOTTOM_Y;                           /* 0x7C038 */
                (void)UiControl_BlitActivePad(pEnv->pCtlConfirm, pEnv->pfnCtlBlit,
                                              pEnv->pfnCtlBlitEx, pEnv->pCtlUser);         /* 0x7C040 */
            }
            (void)snprintf(szScratch, sizeof(szScratch), "%s",
                           PlaneRecText(pEnv->pTextRecs, PUFO_PLANE_TEXT_OK)); /* 0x7C044..0x7C068 */
            (void)Gfx_PackRgb565Pair(pEnv->pColor, 68, 244, 252, 4, 70, 75);   /* 0x7C090 */
            PlaneDetailDrawCentered(120, PUFO_PLANE_BOTTOM_Y, szScratch);
        }

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 200, 200, 200, 10, 25, 10); /* 0x7C0E8 */
        PlaneDetailDrawCentered(120, 30, szName);                           /* 0x7C114 */

        (void)Gfx_PackRgb565Pair(pEnv->pColor, 220, 80, 80, 50, 30, 30); /* 0x7C13C */
        if (nFail != 1u) {
            return 0; /* 0x7C14C: the red pair is packed and nothing is drawn */
        }

        /* 0x7C150..0x7C228: the refusal reason picks the record, and it is a switch -- the buy/sell
         * screen reads the same dword as 405 + reason and lands on different strings. */
        i32 nRec;
        const i32 nReason = (pEnv->pnRefuseReason != NULL) ? *pEnv->pnRefuseReason : -1;
        if (nReason == 0) {
            nRec = PUFO_PLANE_TEXT_NO_ROOM; /* 0x7C220 */
        } else if (nReason == 1) {
            nRec = PUFO_PLANE_TEXT_NO_STORES; /* 0x7C1F4 */
        } else if (nReason == 2) {
            nRec = PUFO_PLANE_TEXT_NO_AMMO; /* 0x7C190 */
        } else {
            return 0; /* 0x7C16C */
        }

        (void)Ui_AnimExpandBlit_IfEnabled(pEnv->pAnim, 20, 130, 200, 70, 1);
        PlaneDetailDrawCentered(120, 158, PlaneRecText(pEnv->pTextRecs, nRec)); /* 0x7C1BC */
        return 0;
    }

    /* ---- the detail arm, 0x7C22C..0x7C9DC ---- */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 200, 200, 200, 10, 25, 10); /* 0x7C250 */
    PlaneDetailDrawCentered(120, 10, szName);                           /* 0x7C270 */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 100, 150, 250, 10, 25, 10); /* 0x7C290 */

    /* 0x7C294..0x7C334: "Топливо " from the RANGE pair. The original divides through _rt_sdiv with no
     * zero test; the port reads 0 percent when the divisor is 0. */
    i32 nRangePct = 0;
    if (pCraft != NULL && pCraft->uRangeMax != 0u) {
        nRangePct = (100 * (i32)pCraft->uRangeRemain) / (i32)pCraft->uRangeMax;
    }
    (void)snprintf(szScratch, sizeof(szScratch), "%s%d%s",
                   PlaneRecText(pEnv->pTextRecs, PUFO_PLANE_TEXT_FUEL), nRangePct, PUFO_PLANE_PERCENT);
    Text_DrawString(150, 30, szScratch);

    /* 0x7C338..0x7C3D0: "Повреждения " from the FUEL pair -- the label and the field do not match. */
    i32 nDamagePct = 100;
    if (pCraft != NULL && (i16)pCraft->uFuelMax != 0) {
        nDamagePct = 100 - (100 * (i32)pCraft->nFuel) / (i32)(i16)pCraft->uFuelMax;
    }
    (void)snprintf(szScratch, sizeof(szScratch), "%s%d%s",
                   PlaneRecText(pEnv->pTextRecs, PUFO_PLANE_TEXT_DAMAGE), nDamagePct, PUFO_PLANE_PERCENT);
    Text_DrawString(10, 30, szScratch);

    /* 0x7C3D4..0x7C444: the craft art, picked by bCraftType; a type outside 0..4 blits nothing. */
    if (pCraft != NULL && pEnv->pScreen != NULL && pCraft->bCraftType < PUFO_CRAFT_TYPE_COUNT) {
        const PadImage *pPad = pEnv->pScreen->apCraftPads[pCraft->bCraftType];
        if (pPad != NULL) {
            PadImage_BlitEx(pPad, PUFO_PLANE_PAD_KEY);
        }
    }

    /* 0x7C448..0x7C5C0: the two mounts, each writing its column into the pad before the blit. */
    if (pCraft != NULL) {
        PlaneDetailBlitMount(pEnv, (i32)pCraft->bWeaponL, PUFO_PLANE_MOUNT_X_LEFT);  /* 0x7C454 LDRSB */
        PlaneDetailBlitMount(pEnv, (i32)pCraft->bWeaponR, PUFO_PLANE_MOUNT_X_RIGHT);  /* 0x7C510 LDRSB */
    }

    /* 0x7C5C0..0x7C5F8: all four counters are zeroed whatever bCanDogfight says. */
    if (pEnv->pnSoldiersAboard != NULL) {
        *pEnv->pnSoldiersAboard = 0;
    }
    if (pEnv->pnSoldiersAtBase != NULL) {
        *pEnv->pnSoldiersAtBase = 0;
    }
    if (pEnv->pnSoldiersUnassigned != NULL) {
        *pEnv->pnSoldiersUnassigned = 0;
    }
    if (pEnv->pnCrewCarried != NULL) {
        *pEnv->pnCrewCarried = 0;
    }

    const bool bDogfight = (pCraft != NULL && pCraft->bCanDogfight != 0u);
    if (bDogfight) {
        PlaneDetailCountSoldiers(pEnv, nCraftIdx, nBaseIdx); /* 0x7C630..0x7C68C */
        const i32 nCarried = Craft_CountWeaponSlotsCarried(pEnv->apObjects, pEnv->nObjectCount,
                                                           nCraftIdx); /* 0x7C6A0..0x7C730 */
        if (pEnv->pnCrewCarried != NULL) {
            *pEnv->pnCrewCarried = nCarried;
        }
    }

    /* 0x7C754: both the gated and the skipped path reach this pair, so it is packed either way. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 250, 250, 250, 10, 10, 25);

    const i32 nAboard = (pEnv->pnSoldiersAboard != NULL) ? *pEnv->pnSoldiersAboard : 0;
    const i32 nCarried = (pEnv->pnCrewCarried != NULL) ? *pEnv->pnCrewCarried : 0;

    if (bDogfight && nAboard > 0) { /* 0x7C76C..0x7C7E0 */
        Font_DrawGlyph(130, 200, (u16)(g_fontSys.nBaseBitsBase + PUFO_PLANE_GLYPH_SOLDIER), 0);
        Text_DrawString(142, 200, PUFO_PLANE_X_GLYPH);
        (void)snprintf(szNum, sizeof(szNum), "%d", nAboard);
        Text_DrawString(150, 200, szNum);
    }
    if (bDogfight && nCarried > 0) { /* 0x7C7E8..0x7C858 */
        Font_DrawGlyph(180, 200, (u16)(g_fontSys.nBaseBitsBase + PUFO_PLANE_GLYPH_CREW), 0);
        Text_DrawString(192, 200, PUFO_PLANE_X_GLYPH);
        (void)snprintf(szNum, sizeof(szNum), "%d", nCarried);
        Text_DrawString(200, 200, szNum);
    }

    if (bDogfight) { /* 0x7C860..0x7C960: the two hold lines, gated on bCanDogfight alone */
        const i32 nCap = (i32)Craft_GetTypeCargoCap(pCraft);
        const i32 nUsed = Base_CalcWeaponStorageUsed(pEnv->apObjects, pEnv->nObjectCount, nCraftIdx);

        (void)snprintf(szScratch, sizeof(szScratch), "%s%d",
                       PlaneRecText(pEnv->pTextRecs, PUFO_PLANE_TEXT_FREE_HOLD), nCap - nUsed);
        Text_DrawString(95, 236, szScratch);

        (void)snprintf(szScratch, sizeof(szScratch), "%s%d",
                       PlaneRecText(pEnv->pTextRecs, PUFO_PLANE_TEXT_TOTAL_HOLD), nCap);
        Text_DrawString(95, 248, szScratch);
    }

    /* 0x7C984: dead on all three branches -- DrawStatLabel packs its own pair before any glyph, and the
     * reset branch draws nothing at all. */
    (void)Gfx_PackRgb565Pair(pEnv->pColor, 100, 150, 250, 10, 10, 25);

    if (pEnv->pStatPanels != NULL && pEnv->pStatPanels->bPanelAOpen != 0) {
        BasePlaneScreen_DrawStatLabel(pEnv->pStatPanels, 0, (u8 *)pEnv->pTextRecs, pEnv->pColor,
                                      (pEnv->pnStatRowCount != NULL) ? *pEnv->pnStatRowCount : 0);
    } else if (pEnv->pStatPanels != NULL && pEnv->pStatPanels->bPanelBOpen != 0) {
        BasePlaneScreen_DrawStatLabel(pEnv->pStatPanels, 1, (u8 *)pEnv->pTextRecs, pEnv->pColor,
                                      (pEnv->pnStatRowCount != NULL) ? *pEnv->pnStatRowCount : 0);
    } else if (pEnv->pStatEnv != NULL) {
        (void)BasePlane_ResetDetailState(pEnv->pStatEnv); /* 0x7C9DC */
    }

    return 0;
}
