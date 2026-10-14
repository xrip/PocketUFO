/* pufo_plane.c - see pufo_plane.h for evidence. */
#include "game/pufo_plane.h"
#include "game/pufo_geoscape.h"   /* BaseRec_AddItemQty */
#include "game/pufo_font.h"       /* Text_DrawString / Text_MeasureWidth, the panel headers */
#include "game/pufo_baseui.h"      /* Soldier_FormatDisplayName, the crew status column */

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
