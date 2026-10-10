/* pufo_combat.c - see pufo_combat.h for the evidence and the layout notes. */
#include "pufo_combat.h"
#include "game/pufo_unit.h"
#include "util/pufo_rand.h"

size_t Combat_HitIndex(i32 nRow, i32 nCol, i32 nSlot)
{
    return (size_t)(4 * (PUFO_SPLASH_SPAN * nRow + nCol) + nSlot);
}

/* --- Unit_SelectBestFireMode @ 0x478e4 -------------------------------------- */

/* One hand's candidate cost and mode. Returns 0xFFFF when the hand is not a
 * candidate. The original assigns pSelWeapon as soon as a hand qualifies, before
 * it knows which hand it will pick, and the port keeps that. */
static i32 Combat_HandCost(Soldier *pUnit, TactItem *pHand, const FireGate *pGate,
                           TactItem **ppSelWeapon, i32 *pnMode)
{
    if (pHand == NULL || pHand->pObj == NULL) {
        return 0xFFFF;
    }
    if (!(pHand->nAmmo > 2 || pHand->pObj->bAmmoCap == 255)) {
        return 0xFFFF;
    }
    if (pHand->pObj->bTwoHanded != 0
        || (pHand->pObj->abArmorTu[0] == 0 && pHand->pObj->abArmorTu[1] == 0)) {
        return 0xFFFF;
    }

    *ppSelWeapon = pHand;
    i32 nCost = 0xFFFF;
    i32 nMode = 0;

    if (pHand->pObj->abArmorTu[1] != 0) {
        i32 nTry = (i32)pUnit->auStat[0] * pHand->pObj->abArmorTu[1] / 100;
        if (nTry <= (i32)pUnit->uTu) {
            nCost = nTry;
            nMode = 1;
        }
    }
    /* Checked second, so mode 0 wins when both fit. */
    if (pHand->pObj->abArmorTu[0] != 0) {
        i32 nTry = (i32)pUnit->auStat[0] * pHand->pObj->abArmorTu[0] / 100;
        if (nTry <= (i32)pUnit->uTu) {
            nCost = nTry;
            nMode = 0;
        }
    }
    if (Unit_bIsOperative(pUnit)
        && !pGate->pfnWeaponFireable(pHand->pObj->uSortKey, pGate->pUser)) {
        nCost = 0xFFFF;
    }
    *pnMode = nMode;
    return nCost;
}

bool Unit_SelectBestFireMode(Soldier *pUnit, const FireGate *pGate,
                             TactItem **ppSelWeapon, i32 *pnActionMode,
                             ActionCostTable *pCosts)
{
    i32 nModeL = 0, nModeR = 0;
    i32 nCostL = Combat_HandCost(pUnit, pUnit->pHandL, pGate, ppSelWeapon, &nModeL);
    i32 nCostR = Combat_HandCost(pUnit, pUnit->pHandR, pGate, ppSelWeapon, &nModeR);

    TactItem *pSel;
    i32 nMode, nCost;
    if (nCostR < 0xFFFF) {
        pSel = pUnit->pHandR;
        nMode = nModeR;
        nCost = nCostR;
    } else if (nCostL < 0xFFFF) {
        pSel = pUnit->pHandL;
        nMode = nModeL;
        nCost = nCostL;
    } else {
        return false;
    }

    *ppSelWeapon = pSel;
    *pnActionMode = nMode;
    pCosts->nAccuracy[nMode] = (i32)((double)pSel->pObj->abCostPct[nMode]
                                     * (double)pUnit->auStat[4] * 0.01);
    pCosts->nTuCost[nMode] = nCost;
    return true;
}

/* --- Unit_ApplyCellDamage @ 0x2e5d0 ----------------------------------------- */

/* Readiness source, selected by the weapon record's two selector bytes.
 * The five values all live in the unit: Soldier.abReady252[0..4] at 252..256.
 * When a selector is set but not one of the known values the original leaves the
 * scale from the previous item in place; the port keeps that behaviour by taking
 * the running value as an in/out argument. */
static i32 Combat_ReadyScale(i32 nScale, const Soldier *pUnit,
                             const ObjectRecFull *pWeapon)
{
    if (pWeapon->bFireType != 0) {
        if (pWeapon->bFireType == 1) {
            nScale = pUnit->abReady252[2];
        } else if (pWeapon->bFireType == 2) {
            nScale = pUnit->abReady252[0];
        }
    } else if (pWeapon->bReadySel != 0) {
        if (pWeapon->bReadySel == 2) {
            nScale = pUnit->abReady252[1];
        } else if (pWeapon->bReadySel == 3) {
            nScale = pUnit->abReady252[3];
        }
    } else {
        nScale = pUnit->abReady252[4];
    }
    return 1 - nScale / 100;
}

/* The damage/armour/stun block. It is the same code in Unit_ApplyCellDamage and
 * in the slot-3 path of the splash walk, so it lives here once. */
static void Combat_HurtUnit(Soldier *pUnit, i32 nDmg)
{
    if (nDmg < 0) {
        nDmg = 0;
    }
    if (pUnit->nArmorPoints > 0) {
        pUnit->nArmorPoints -= nDmg;
        if (pUnit->nArmorPoints >= 0) {
            nDmg = 0;
        } else {
            i32 nSpill = pUnit->nArmorPoints;
            pUnit->nArmorPoints = 0;
            nDmg = -nSpill;
        }
    }
    if (nDmg > (i32)pUnit->uHp) {
        pUnit->uHp = 0;
    } else {
        pUnit->uHp = (u16)(pUnit->uHp - nDmg);
        pUnit->nUnk436 += nDmg;
    }
    if (nDmg >= (i32)(pUnit->auStat[1] >> 1) && !Unit_bIsLarge(pUnit)) {
        pUnit->bKneelOrPrimed = (u8)(pUnit->bKneelOrPrimed
            + (nDmg - (i32)(pUnit->auStat[1] >> 1)) / 10 + 1);
    }
    pUnit->gap_433 = 1;
}

void Unit_ApplyCellDamage(MapCell *pCell, u16 nPower, const ObjectRecFull *pWeapon)
{
    i32 nScale = 0;

    for (;;) {
        bool bRemoved = false;

        for (TactItem *pItem = pCell->pItemList; pItem != NULL;
             pItem = pItem->pNext) {
            Soldier *pUnit = pItem->pLinkedSoldier;

            if (pUnit != NULL && pUnit->gap_433 == 0) {
                nScale = Combat_ReadyScale(nScale, pUnit, pWeapon);
                i32 nDmg = (i32)nPower - (i32)pUnit->bUnk248 * nScale;
                nDmg -= (i32)Game_RandU16((u16)(nDmg / 4));
                Combat_HurtUnit(pUnit, nDmg);
            }
            if ((pUnit == NULL || pUnit->uHp == 0) && nPower > 40) {
                MapCell_ListRemove(pCell, pItem);
                bRemoved = true;
                break;
            }
        }
        if (!bRemoved) {
            break;
        }
    }
}

/* --- Combat_SplashDamageCells @ 0x2e7d8 ------------------------------------- */

/* Mode 2 (throw): a tile power at or above the sprite's own threshold rewrites
 * the cell's flag fields, then the low three bits of both flag words are
 * randomised unless nFlags32 already holds exactly 4. */
static void Combat_ThrowFlags(MapCell *pCell, const Mc1Rec *pSpr, u16 nPower,
                              const ObjectRecFull *pCtxWeapon)
{
    if (pCtxWeapon->uSortKey == PUFO_WEAPON_SORT_50
        || pCtxWeapon->uSortKey == PUFO_WEAPON_SORT_15) {
        return;
    }
    if (pSpr != NULL && nPower >= pSpr->bSplashResist) {
        pCell->nFlags32 = (pCell->nFlags32 & ~7u) | 4u;
        pCell->nFlags40 = (pCell->nFlags40 & ~7u) | 2u;
        pCell->bFlags36 &= ~1u;
    }
    if ((pCell->nFlags32 & 7u) == 4u) {
        return;
    }
    u16 nRoll = Game_RandU16(3);
    if ((pCell->nFlags32 & 7u) < nRoll) {
        pCell->nFlags32 ^= (u32)(((u8)pCell->nFlags32 ^ (u8)nRoll) & 7u);
    }
    nRoll = (u16)(Game_RandU16(2) + 2);
    if ((pCell->nFlags40 & 7u) < nRoll) {
        pCell->nFlags40 ^= (u32)(((u8)pCell->nFlags40 ^ (u8)nRoll) & 7u);
    }
}

/* Mode 1: floor and ceiling tiles only, and no threshold test. */
static void Combat_SecondaryFlags(MapCell *pCell, const Mc1Rec *pSpr, i32 nSlot)
{
    if (pSpr == NULL || (nSlot != 0 && nSlot != 3)) {
        return;
    }
    pCell->nFlags32 = (pCell->nFlags32 & ~7u) | 4u;
    u8 nRoll = (u8)(Game_RandU16(2) + 2);
    pCell->nFlags40 ^= (u32)((nRoll ^ (u8)pCell->nFlags40) & 7u);
    pCell->bFlags36 |= 1u;
}

/* Slot 3: the occupant takes the slot-3 power. The selected unit is exempt in
 * mode 0, and a unit already marked by this shot (gap_433) is skipped. */
static void Combat_DamageOccupant(const CombatCtx *pCtx, MapCell *pCell,
                                  i32 nUnitPower, SplashMode eMode, i32 *pnScale)
{
    if (!MapCell_HasOccupant(pCell)) {
        return;
    }
    Soldier *pUnit = pCell->pUnit;
    if (pUnit->gap_433 != 0) {
        return;
    }
    if (pCtx->pCtxWeapon->uSortKey == PUFO_WEAPON_SORT_50
        || pCtx->pCtxWeapon->uSortKey == PUFO_WEAPON_SORT_15) {
        return;
    }
    i32 nReady;
    switch (eMode) {
    case SPLASH_MODE_NORMAL:
        if (pUnit == pCtx->pSelUnit) {
            return;
        }
        nReady = pUnit->abReady252[0];
        break;
    case SPLASH_MODE_SECONDARY:
        nReady = pUnit->abReady252[2];
        break;
    case SPLASH_MODE_THROW:
        nReady = pUnit->abReady252[0];
        break;
    default:
        /* The original reuses the scale left over from the previous cell. */
        nReady = *pnScale;
        break;
    }
    *pnScale = 1 - nReady / 100;

    i32 nDmg = nUnitPower - (i32)pUnit->bUnk248 * *pnScale;
    nDmg -= (i32)Game_RandU16((u16)(nDmg / 4));
    Combat_HurtUnit(pUnit, nDmg);
    Unit_EnqueueIfApSpent(pCtx->pState, pUnit, 1);
}

/* Slot 0 only. Bits 3..6 of nFlags32 are replaced by one of four patterns. */
static void Combat_MutateFlags(MapCell *pCell, const ObjectRecFull *pShotWeapon,
                               u16 nPower, SplashMode eMode)
{
    if (eMode == SPLASH_MODE_NORMAL) {
        return;
    }
    if (nPower <= PUFO_POWER_MIN) {
        return;
    }
    if (pShotWeapon->uSortKey == PUFO_WEAPON_SORT_50 && nPower <= PUFO_POWER_HIGH) {
        return;
    }
    u8 nRoll = (u8)(8 * Game_RandU16(3) - 57);
    pCell->nFlags32 ^= (u32)((nRoll ^ (u8)pCell->nFlags32) & 0x78u);
}

/* Sprite damage. `d` is the power over the sprite's own hardness; while the
 * accumulated per-slot counter stays under the hardness the sprite only records
 * the hit, otherwise it is destroyed or replaced by its extra variant. */
static void Combat_DamageSprite(MapCell *pCell, i32 nSlot, u16 nPower,
                                Mc1Rec *pSpr)
{
    i32 d = (i32)((double)nPower - 1.5 * (double)pSpr->bStrength);
    u8 *pCounter = &pCell->bUnk16 + nSlot;

    if ((i32)pSpr->bStrength >= (i32)*pCounter + d) {
        if (d < 0) {
            d = 0;
        }
        i32 nSum = (i32)*pCounter + d;
        *pCounter = (nSum >= 255) ? 0xFFu : (u8)nSum;
        return;
    }
    if (nSlot == 1) {
        if (pCell->nZ != 0) {
            pCell->pWallSprA = NULL;
        } else {
            pCell->pWallSprA = Rec_GetExtraSpr(pCell->pWallSprA->nSlot,
                                               pCell->pWallSprA->bExtraSprFacing);
        }
        return;
    }
    if (nSlot == 2) {
        if (pCell->nZ != 0) {
            pCell->pWallSprB = NULL;
        } else {
            pCell->pWallSprB = Rec_GetExtraSpr(pCell->pWallSprB->nSlot,
                                               pCell->pWallSprB->bExtraSprFacing);
        }
        return;
    }
    if (nSlot == 3) {
        pCell->pCeilSpr = Rec_GetExtraSpr(pCell->pCeilSpr->nSlot,
                                          pCell->pCeilSpr->bExtraSprFacing);
        return;
    }
    /* Floor slot. With no extra variant the sprite survives only on layer 0.
     * With a variant it is swapped for that sprite, and on layer 0 the swap is
     * always kept; higher layers drop it when the variant is no harder than the
     * damage already recorded. */
    if (pSpr->bExtraSprFacing == 0) {
        if (pCell->nZ != 0) {
            pCell->pFloorSpr = NULL;
        }
        return;
    }
    pCell->bUnk16 = (pCell->nZ != 0) ? (u8)(d - (i32)pSpr->bStrength) : 0;
    pCell->pFloorSpr = Rec_GetExtraSpr(pSpr->nSlot, pSpr->bExtraSprFacing);
    if (pCell->nZ != 0 && pCell->pFloorSpr != NULL
        && (i32)pCell->pFloorSpr->bStrength <= pCell->bUnk16) {
        pCell->pFloorSpr = NULL;
    }
}

/* Weapon class 50: instead of destroying the ceiling tile it drains TU from the
 * occupant until it is unconscious. */
static void Combat_Class50TuDrain(const CombatCtx *pCtx, MapCell *pCell,
                                  const ObjectRecFull *pShotWeapon, i32 nUnitPower)
{
    if (nUnitPower <= 0x41) {
        return;
    }
    if (!MapCell_HasOccupant(pCell)) {
        return;
    }
    Soldier *pUnit = pCell->pUnit;
    pUnit->uTuSpent = (u16)(pUnit->uTuSpent + pShotWeapon->uFirePower);
    if (pUnit->uTuSpent >= (u16)pUnit->uHp) {
        pUnit->uTuSpent = (u16)(10 * (Game_RandU16(6) + 7) + pUnit->uHp);
    }
    Unit_EnqueueIfApSpent(pCtx->pState, pUnit, 0);
}

/* Weapon class 15: the tile collapses, and the collapse spreads straight up
 * through the layers above until a lookup returns no cell. */
static void Combat_Class15Collapse(const CombatCtx *pCtx, MapCell *pCell,
                                   i32 nUnitPower)
{
    if (pCell->pFloorSpr == NULL || nUnitPower <= 0xA) {
        return;
    }
    pCell->nFlags32 = (pCell->nFlags32 & ~7u) | 3u;
    pCell->nFlags40 = (pCell->nFlags40 & ~7u) | 3u;
    for (i32 nLayer = pCell->nZ + 1; nLayer < PUFO_MAP_LAYERS; nLayer++) {
        MapCell *pUp = pCtx->pfnLookup(pCell->nX, pCell->nY, nLayer, pCtx->pUser);
        if (pUp == NULL) {
            break;
        }
        pUp->nFlags32 = (pUp->nFlags32 & ~7u) | 3u;
        pUp->nFlags40 = (pUp->nFlags40 & ~7u) | 3u;
    }
}

void Combat_SplashDamageCells(const CombatCtx *pCtx, i32 nX, i32 nY, i32 nLayer,
                              const ObjectRecFull *pShotWeapon, const u32 *apHits,
                              SplashMode eMode)
{
    i32 nScale = 0;

    for (i32 nRow = 0; nRow < PUFO_SPLASH_SPAN; nRow++) {
        for (i32 nCol = 0; nCol < PUFO_SPLASH_SPAN; nCol++) {
            i32 nCellX = nX - PUFO_SPLASH_HALF + nCol;
            i32 nCellY = nY - PUFO_SPLASH_HALF + nRow;

            if (!Map_IsValidCell(pCtx->pGeom, nCellX, nCellY, nLayer)) {
                continue;
            }
            MapCell *pCell = pCtx->pfnLookup(nCellX, nCellY, nLayer, pCtx->pUser);
            if (pCell == NULL) {
                continue;
            }
            for (i32 nSlot = 0; nSlot < 4; nSlot++) {
                u32 nSlotPower = apHits[Combat_HitIndex(nRow, nCol, nSlot)];
                if ((u16)nSlotPower == 0) {
                    continue;
                }
                Mc1Rec **ppSpr = nSlot == 0 ? &pCell->pFloorSpr
                           : nSlot == 1 ? &pCell->pWallSprA
                           : nSlot == 2 ? &pCell->pWallSprB
                                        : &pCell->pCeilSpr;
                Mc1Rec *pSpr = *ppSpr;
                u16 nPower = (u16)apHits[Combat_HitIndex(nRow, nCol, 0)];
                i32 nUnitPower = (i32)(u16)apHits[Combat_HitIndex(nRow, nCol, 3)];

                if (eMode == SPLASH_MODE_THROW) {
                    Combat_ThrowFlags(pCell, pSpr, nSlotPower, pCtx->pCtxWeapon);
                } else if (eMode == SPLASH_MODE_SECONDARY) {
                    Combat_SecondaryFlags(pCell, pSpr, nSlot);
                }
                if (nSlot != 0) {
                    /* Slots 1..3 jump straight past the tile-damage rules; only the
                     * ceiling slot carries the unit power. */
                    if (nSlot == 3) {
                        Combat_DamageOccupant(pCtx, pCell, nUnitPower, eMode,
                                              &nScale);
                    }
                } else {
                    if ((u16)nSlotPower > PUFO_POWER_MIN
                        && pCtx->pCtxWeapon->uSortKey != PUFO_WEAPON_SORT_50
                        && pCtx->pCtxWeapon->uSortKey != PUFO_WEAPON_SORT_15) {
                        Unit_ApplyCellDamage(pCell, (u16)nSlotPower, pShotWeapon);
                    }
                    Combat_MutateFlags(pCell, pShotWeapon, (u16)nSlotPower, eMode);
                }

                u16 nCtxClass = pCtx->pCtxWeapon->uSortKey;
                if (pSpr == NULL || pSpr->bStrength == 255
                    || nCtxClass == PUFO_WEAPON_SORT_50
                    || nCtxClass == PUFO_WEAPON_SORT_15) {
                    /* The original leaves the slot here unless it is the ceiling
                     * slot; the two special weapon classes only act there. */
                    if (nSlot == 3) {
                        if (nCtxClass == PUFO_WEAPON_SORT_50) {
                            Combat_Class50TuDrain(pCtx, pCell, pShotWeapon,
                                                  nUnitPower);
                        } else if (nCtxClass == PUFO_WEAPON_SORT_15) {
                            Combat_Class15Collapse(pCtx, pCell, nUnitPower);
                        }
                    }
                    continue;
                }
                Combat_DamageSprite(pCell, nSlot, nPower, pSpr);
            }
        }
    }
}
