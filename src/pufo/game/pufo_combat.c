/* pufo_combat.c - see pufo_combat.h for the evidence and the layout notes. */
#include "pufo_combat.h"
#include "audio/pufo_audio.h"
#include "game/pufo_unit.h"
#include "game/pufo_tactctl.h"   /* MapView_FocusOnCell, FocusCtx, TactCursor */
#include "game/pufo_vistry.h"   /* Map_ScanBlastFootprint, the second gate on every proximity device */
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

/* --- Combat_ResolveShot @ 0x2f12c ------------------------------------------- */

/* off_AA040 @ 0xaa040 and word_AA168 @ 0xaa168, 148 u16 each, indexed by
 * ObjectRecFull.uSortKey. Dumped from the binary. */
const u16 g_aShotSound[PUFO_SHOT_SOUND_COUNT] = {
    18, 18, 4, 18, 18, 4, 18, 18, 18, 4, 18, 18,
    18, 18, 2, 2, 2, 2, 2, 2, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 9, 1, 4, 4, 1,
    9, 4, 9, 4, 10, 2, 2, 10, 2, 2, 1, 1,
    1, 4, 1, 1, 18, 18, 18, 18, 10, 1, 1, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 0
};
const u16 g_aFireSoundBySortKey[PUFO_SHOT_SOUND_COUNT] = {
    20, 8, 5, 5, 8, 5, 5, 3, 3, 5, 11, 5,
    11, 11, 18, 18, 18, 18, 18, 18, 18, 18, 20, 18,
    21, 18, 18, 18, 18, 18, 18, 8, 11, 5, 5, 11,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18,
    18, 18, 18, 0
};

static i32 Combat_ShotSound(const ObjectRecFull *pWeapon)
{
    /* The original indexes the table with no bound check. uSortKey is a u16 and
     * the table has 180 entries, so a larger key reads past the table. The port
     * clamps; see the parity note. */
    return (pWeapon->uSortKey < PUFO_SHOT_SOUND_COUNT)
               ? (i32)g_aShotSound[pWeapon->uSortKey] : 0;
}

/* 0x2f180..0x2f248: clear the per-shot "already hit" byte (Soldier+0x1B1) on
 * every unit in all three rosters. Unit_ApplyCellDamage and the splash walk use
 * it to hit each unit once per shot. */
static void Combat_ClearShotFlags(const UnitRosters *pR)
{
    for (i32 i = 0; i < pR->nXcom; ++i)  { pR->apXcom[i]->gap_433 = 0; }
    for (i32 i = 0; i < pR->nEnemy; ++i) { pR->apEnemy[i]->gap_433 = 0; }
    for (i32 i = 0; i < pR->nCiv; ++i)   { pR->apCiv[i]->gap_433 = 0; }
}

/* 0x2f408..0x2f574: the direct unit hit. Damage is the weapon's fire power less
 * the target's armour, cut by a random amount up to half, soaked by armour
 * first, then health. A hit at least as strong as half the target's auStat[1]
 * also knocks it down, unless it is a large unit. */
static i32 Combat_DirectUnitDamage(const CombatCtx *pCtx, MapCell *pCell,
                                   const ObjectRecFull *pWeapon)
{
    Soldier *pUnit = pCell->pUnit;
    if (pUnit == NULL) {
        /* 0x2f410: re-read the same cell through the context cell grid and take
         * its unit. Map and pCtxBase are the same object, so this normally finds
         * nothing new. */
        MapCell *pAgain = pCtx->pfnLookup(pCell->nX, pCell->nY, pCell->nZ,
                                          pCtx->pUser);
        pUnit = pAgain->pUnit;
    }

    /* 0x2f44c: which readiness byte to subtract armour with, chosen by the
     * weapon's bReadySel. Any other value makes the original read an
     * uninitialised stack slot; the port uses zero and records the deviation. */
    i32 nReady;
    switch (pWeapon->bReadySel) {
        case 0: nReady = pUnit->abReady252[4]; break;
        case 2: nReady = pUnit->abReady252[1]; break;
        case 3: nReady = pUnit->abReady252[3]; break;
        default: nReady = 0; break;
    }

    /* (1 - nReady / 100) is integer arithmetic: a target at or above 100
     * readiness loses no armour to the hit. */
    i32 nPower = (i32)pWeapon->uFirePower
               - (1 - nReady / 100) * (i32)pUnit->bUnk248;
    i32 nDamage = nPower - (i32)Game_RandU16((u16)(nPower / 2));
    if (nDamage < 0) {
        nDamage = 0;
    }

    /* 0x2f4d8: `if (nArmorPoints > 150) nullsub_4();` - a call to an empty
     * function, noted rather than reproduced. */
    if (pUnit->nArmorPoints > 0) {
        i32 nLeft = pUnit->nArmorPoints - nDamage;
        pUnit->nArmorPoints = nLeft;
        if (nLeft >= 0) {
            nDamage = 0;
        } else {
            nDamage = -nLeft;
            pUnit->nArmorPoints = 0;
        }
    }

    if (nDamage > (i32)pUnit->uHp) {
        pUnit->uHp = 0;
    } else {
        pUnit->uHp = (u16)(pUnit->uHp - (u16)nDamage);
        pUnit->nUnk436 += nDamage;
    }

    if (nDamage >= (pUnit->auStat[1] >> 1) && !Unit_bIsLarge(pUnit)) {
        pUnit->bKneelOrPrimed = (u8)(pUnit->bKneelOrPrimed
            + (nDamage - (pUnit->auStat[1] >> 1)) / 10 + 1);
    }

    Unit_EnqueueIfApSpent(pCtx->pState, pUnit, 0);
    return nDamage;
}

/* 0x2f328..0x2f734: walk the hit window looking for ceiling tiles the shot
 * broke. Each cell's unit power (the low half of hit dword 3) plus the cell's
 * own bUnk19 is compared against the ceiling sprite's strength. The last cell
 * that gave way becomes the impact point of the splash walk, and the walk mode
 * is promoted to SPLASH_MODE_THROW. */
static void Combat_CeilingBreakSweep(const CombatCtx *pCtx,
                                     const ShotHooks *pHooks, i32 nLayer,
                                     i32 nOriginX, i32 nOriginY, u32 *apHits,
                                     SplashMode *peMode, i32 *pnImpactX,
                                     i32 *pnImpactY, i32 *pnFovCalls)
{
    for (i32 nRow = 0; nRow < PUFO_SPLASH_SPAN; ++nRow) {
        for (i32 nCol = 0; nCol < PUFO_SPLASH_SPAN; ++nCol) {
            i32 nX = nCol + nOriginX - PUFO_SPLASH_HALF;
            i32 nY = nRow + nOriginY - PUFO_SPLASH_HALF;
            if (!Map_IsValidCell(pCtx->pGeom, nX, nY, nLayer)) {
                continue;
        }
            size_t nIdx = Combat_HitIndex(nRow, nCol, 3);
            u16 nUnitPower = (u16)apHits[nIdx];
            if (nUnitPower == 0) {
                continue;
            }
            MapCell *pCell = pCtx->pfnLookup(nX, nY, nLayer, pCtx->pUser);
            Mc1Rec *pCeil = pCell->pCeilSpr;
            if (pCeil == NULL || pCeil->bCeilBreakable == 0) {
                continue;
            }
            if ((i32)nUnitPower + (i32)pCell->bUnk19 <= (i32)pCeil->bStrength) {
                continue;
            }
            if (*peMode == SPLASH_MODE_NORMAL) {
                *peMode = SPLASH_MODE_THROW;
                Sound_Play(PUFO_SOUND_CEILING_BREAK);
            }
            *pnImpactX = nX;
            *pnImpactY = nY;
            /* 0x2f698: (double)sprite+0x18 * 6.0, converted to int and stored
             * into a spilled slot the decompiler names nRange. No use of that
             * slot survives in the function, so the port does not compute it. */
            pHooks->pfnComputeFov(pHooks->pUser);
            (*pnFovCalls)++;
            /* 0x2f6f8: mark the record by writing 1 into the high half of dword 3. */
            apHits[nIdx] = (apHits[nIdx] & 0x0000FFFFu) | 0x00010000u;
        }
    }
}

/* 0x2f7ac..0x2f894 and 0x2f914..0x2f9f4: attenuate the hit window for a layer
 * above or below the impact layer. The reduction is 20 when the shot became
 * area fire and 4 otherwise, then the floor sprite of the layer being processed
 * takes another bite. Cell validity is still tested against the shot's own
 * layer, not the layer being processed - that is how the original writes it. */
static void Combat_AttenuateLayer(const CombatCtx *pCtx, u32 *apHits,
                                  i32 nLayerTest, i32 nLayerFloor, i32 nImpactX,
                                  i32 nImpactY, SplashMode eMode)
{
    for (i32 nRow = 0; nRow < PUFO_SPLASH_SPAN; ++nRow) {
        for (i32 nCol = 0; nCol < PUFO_SPLASH_SPAN; ++nCol) {
            i32 nX = nCol + nImpactX - PUFO_SPLASH_HALF;
            i32 nY = nRow + nImpactY - PUFO_SPLASH_HALF;
            if (!Map_IsValidCell(pCtx->pGeom, nX, nY, nLayerTest)) {
                continue;
        }
            for (i32 nSlot = 0; nSlot < 4; ++nSlot) {
                size_t nIdx = Combat_HitIndex(nRow, nCol, nSlot);
                i32 nValue = (i32)(u16)apHits[nIdx];
                nValue -= (eMode == SPLASH_MODE_THROW) ? 20 : 4;
                if (nValue > 0) {
                    MapCell *pCell = pCtx->pfnLookup(nX, nY, nLayerFloor,
                                                     pCtx->pUser);
                    if (pCell->pFloorSpr != NULL) {
                        nValue -= (i32)pCell->pFloorSpr->bStrength;
                    }
                }
                if (nValue <= 0) {
                    nValue = 0;
                }
                apHits[nIdx] = (apHits[nIdx] & 0xFFFF0000u) | (u32)(u16)nValue;
            }
        }
    }
}

/* 0x2fa9c..0x2fc14: items left on a cell with no floor sprite fall to the
 * highest layer below that has one. Layers 1..3 are scanned; layer 0 cannot
 * fall. */
static void Combat_DropCellItems(const CombatCtx *pCtx)
{
    const i32 nCellsX = 10 * (i32)pCtx->pGeom->nDimX;
    const i32 nCellsY = 10 * (i32)pCtx->pGeom->nDimY;

    for (i32 nLayer = 1; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (i32 nY = 0; nY < nCellsY; ++nY) {
            for (i32 nX = 0; nX < nCellsX; ++nX) {
                MapCell *pCell = pCtx->pfnLookup(nX, nY, nLayer, pCtx->pUser);
                if (pCell == NULL || pCell->pItemList == NULL
                    || pCell->pFloorSpr != NULL) {
                    continue;
                }
                i32 nTarget = nLayer;
                while (nTarget >= 0) {
                    MapCell *pBelow = pCtx->pfnLookup(nX, nY, nTarget,
                                                      pCtx->pUser);
                    if (pBelow != NULL && pBelow->pFloorSpr != NULL) {
                        break;
                    }
                    --nTarget;
                }
                if (nTarget == nLayer) {
                    continue;
                }
                /* Deviation: with no floor sprite anywhere below, the original
                 * indexes layer -1 of the cell grid. The port leaves the items
                 * where they are instead. */
                if (nTarget < 0) {
                    continue;
                }
                MapCell *pDst = pCtx->pfnLookup(nX, nY, nTarget, pCtx->pUser);
                while (pCell->pItemList != NULL) {
                    TactItem *pNode = pCell->pItemList;
                    MapCell_ListRemove(pCell, pNode);
                    MapCell_ListInsertByPri(pDst, pNode);
                }
            }
        }
    }
}

/* The two halves of the detonation tail. The original writes the origin, then focuses only
 * when the cell's bFlags44 bit 0 is set, and records that it moved the view. */
static void Proximity_SetOrigin(ProximityCtx *pCtx, MapCell *pCell)
{
    pCtx->pCombat->nShotOriginX = pCell->nX;
    pCtx->pCombat->nShotOriginY = pCell->nY;
}

static void Proximity_FocusIfFlagged(ProximityCtx *pCtx, MapCell *pCell)
{
    if ((pCell->bFlags44 & 1) != 0) {
        MapView_FocusOnCell(pCtx->pFocus, pCell, pCtx->pCursor);
        if (pCtx->pbViewMoved != NULL) {
            *pCtx->pbViewMoved = 1;
        }
    }
}

static void Proximity_Resolve(ProximityCtx *pCtx, MapCell *pCell, ObjectRec *pObj)
{
    Combat_ResolveShot(pCtx->pCombat, pCtx->pShotHooks, pCtx->pRosters,
                       pCell->nX, pCell->nY, pCell->nZ, 0, pObj, NULL, NULL, NULL);
}

/* MapCell_TickProximityItems @ 0x4b724.
 *
 * Hex-Rays renders the shot argument at 0x4B900 as pItemList->pObj->gap_0, which looks like a
 * read through the object. Case 15 settles it: v8 = (int *)pItemList and the argument is *v8,
 * which is the TactItem's own pObj field. Both paths pass the object pointer. The default case
 * removes the node before the shared tail, but MapCell_ListRemove @ 0x54b58 only relinks and
 * never frees, so the pointer is still good. */
void MapCell_TickProximityItems(ProximityCtx *pCtx, MapCell *pCell)
{
    if (pCtx == NULL || pCell == NULL || pCtx->pCombat == NULL ||
        pCtx->pGeom == NULL || pCtx->pfnLookupCell == NULL || pCtx->paSteps == NULL) {
        return;
    }

    if (pCtx->pbViewMoved != NULL) {
        *pCtx->pbViewMoved = 0;                     /* 0x4B738 */
    }

    for (TactItem *pItem = pCell->pItemList; pItem != NULL; ) {
        ObjectRec *pObj = pItem->pObj;

        if (pObj == NULL || pObj->nCategory != 3 || pItem->bUnk8 == 0) {
            pItem = pItem->pNext;
            continue;
        }

        /* 0x4B770: decrement first, then test. The compare is signed char, so a countdown that
         * is already zero becomes -1 and detonates again on every later call. */
        i8 nCountdown = (i8)(u8)(pItem->bUnk7 - 1);
        pItem->bUnk7 = (u8)nCountdown;
        if (nCountdown > 0) {
            pItem = pItem->pNext;
            continue;
        }

        TactItem *pRemove = pItem;

        switch (pObj->uSortKey) {
        case PUFO_PROXIMITY_KEY_MARK:
            /* 0x4B79C: no detonation, only the animated-tile flag field. */
            pCell->nFlags40 = (pCell->nFlags40 & 0xFFFFFF87u) | 0x38u;
            pItem = pItem->pNext;
            break;

        case PUFO_PROXIMITY_KEY_DETONATE:
            pItem = pItem->pNext;
            Proximity_SetOrigin(pCtx, pCell);
            Proximity_FocusIfFlagged(pCtx, pCell);
            Proximity_Resolve(pCtx, pCell, pObj);
            break;

        case PUFO_PROXIMITY_KEY_BLAST:
            /* 0x4B850: a blocked footprint stops the whole cell, not just this item. */
            if (!Map_ScanBlastFootprint(pCtx->pGeom, pCtx->pfnLookupCell,
                                        pCtx->pLookupUser, pCtx->paSteps, pCell)) {
                return;
            }
            Proximity_SetOrigin(pCtx, pCell);
            Proximity_FocusIfFlagged(pCtx, pCell);
            Proximity_Resolve(pCtx, pCell, pObj);
            return;   /* 0x4B91C: a blast device is never removed from the cell */

        default:
            /* 0x4B8C0..0x4B8FC: origin, then removal, then the focus. */
            Proximity_SetOrigin(pCtx, pCell);
            MapCell_ListRemove(pCell, pRemove);
            Proximity_FocusIfFlagged(pCtx, pCell);
            Proximity_Resolve(pCtx, pCell, pObj);
            return;
        }

        MapCell_ListRemove(pCell, pRemove);         /* 0x4B7A8, reached from 14 and 15 only */
    }
}

/* Combat_DetonateNearbyItems @ 0x32fa8.
 *
 * Hex-Rays renders the loop bounds as &result[-1].bUnk56 and &result->pWallSprA, which is
 * nonsense produced by treating a coordinate as a MapCell *. ARM at 0x32FC8 is plain:
 * LDRB R0,[R1,#0x1A] is MapCell.nX and SUB/ADD #4 give the x bounds; LDRB R0,[R1,#0x1B] is
 * MapCell.nY and SUB/ADD #4 give the y bounds. The outer loop is v5 += 60 while v5 < 240,
 * which is the four layers. */
void Combat_DetonateNearbyItems(ProximityCtx *pCtx, const MapCell *pSrcCell)
{
    i32 nX0, nX1, nY0, nY1;

    if (pCtx == NULL || pSrcCell == NULL || pCtx->pCombat == NULL ||
        pCtx->pGeom == NULL || pCtx->pfnLookupCell == NULL || pCtx->paSteps == NULL) {
        return;
    }

    if (pCtx->pbBlastScanFlag != NULL) {
        *pCtx->pbBlastScanFlag = 0;                 /* 0x32FCC */
    }

    nX0 = (i32)pSrcCell->nX - PUFO_PROXIMITY_BOX_RADIUS;
    nX1 = (i32)pSrcCell->nX + PUFO_PROXIMITY_BOX_RADIUS;
    nY0 = (i32)pSrcCell->nY - PUFO_PROXIMITY_BOX_RADIUS;
    nY1 = (i32)pSrcCell->nY + PUFO_PROXIMITY_BOX_RADIUS;

    for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (i32 nY = nY0; nY < nY1; ++nY) {
            for (i32 nX = nX0; nX < nX1; ++nX) {
                if (!Map_IsValidCell(pCtx->pGeom, nX, nY, nLayer)) {
                    continue;
                }

                MapCell *pCell = pCtx->pfnLookupCell(nX, nY, nLayer, pCtx->pLookupUser);
                if (pCell == NULL) {
                    continue;
                }

                /* 0x33104: after a detonation the walk starts again at the head of this
                 * cell's list, so a cell with several devices is drained one at a time. */
                for (;;) {
                    bool bDetonated = false;

                    for (TactItem *pItem = pCell->pItemList; pItem != NULL; pItem = pItem->pNext) {
                        ObjectRec *pObj = pItem->pObj;

                        if (pObj == NULL || pObj->nCategory != 3 || pItem->bUnk8 == 0 ||
                            pObj->uSortKey != PUFO_PROXIMITY_KEY_BLAST ||
                            (i8)pItem->bUnk7 > 0) {
                            continue;
                        }
                        if (!Map_ScanBlastFootprint(pCtx->pGeom, pCtx->pfnLookupCell,
                                                    pCtx->pLookupUser, pCtx->paSteps, pCell)) {
                            continue;
                        }

                        pCtx->pCombat->nShotOriginX = pCell->nX;   /* 0x330C0 */
                        pCtx->pCombat->nShotOriginY = pCell->nY;   /* 0x330D0 */
                        Combat_ResolveShot(pCtx->pCombat, pCtx->pShotHooks, pCtx->pRosters,
                                           pCell->nX, pCell->nY, pCell->nZ, 0, pObj,
                                           NULL, NULL, NULL);
                        MapCell_ListRemove(pCell, pItem);
                        bDetonated = true;
                        break;
                    }
                    if (!bDetonated) {
                        break;
                    }
                }
            }
        }
    }
}

void Combat_ResolveShot(CombatCtx *pCtx, const ShotHooks *pHooks,
                        const UnitRosters *pRosters, i32 nTgtX, i32 nTgtY,
                        i32 nLayer, i32 nSlot, const ObjectRecFull *pWeapon,
                        u32 *apHits, u32 *apHitsSave, ShotOutcome *pOut)
{
    /* The original reaches the 41 x 41 x 4 window and the outcome through Map globals,
     * so it always has both. The port lifted them into parameters, and two callers -
     * the type-18 death explosion at 0x2f12c's caller in pufo_unit.c and the proximity
     * detonation - pass NULL for both, which writes through a null window. A shared
     * scratch pair reproduces the original's single window for callers that do not
     * inspect the result. */
    static u32        s_aScratchHits[PUFO_SPLASH_DWORDS];
    static ShotOutcome s_scratchOut;

    if (apHits == NULL) {
        apHits = s_aScratchHits;
    }
    if (pOut == NULL) {
        pOut = &s_scratchOut;
    }

    Combat_ClearShotFlags(pRosters);

    /* 0x2f254: the splash walk reads the firing weapon back out of the map. */
    pCtx->pCtxWeapon = (ObjectRecFull *)pWeapon;

    /* 0x2f258..0x2f284: clear the whole 41 x 41 x 4 window. */
    for (size_t i = 0; i < PUFO_SPLASH_DWORDS; ++i) {
        apHits[i] = 0;
    }

    SplashMode eMode = (SplashMode)pWeapon->bFireType;
    i32 nImpactX = nTgtX;
    i32 nImpactY = nTgtY;
    i32 nFovCalls = 0;
    pOut->nDamage = -1;

    if (pWeapon->bFireType != 0) {
        /* 0x2f2c0: area fire. Modes other than 1 and 2 skip the sound and the
         * field-of-view refresh and go straight to the splash walk. */
        if (pWeapon->bFireType == 1 || pWeapon->bFireType == 2) {
            Sound_Play(Combat_ShotSound(pWeapon));
            nImpactX = pCtx->nShotOriginX;
            nImpactY = pCtx->nShotOriginY;
            pHooks->pfnComputeFov(pHooks->pUser);
            nFovCalls++;
        }
    } else {
        Sound_Play(Combat_ShotSound(pWeapon));
        /* 0x2f3dc: the impact record, at the centre of the window, in the slot
         * the shot landed in. */
        apHits[Combat_HitIndex(PUFO_SPLASH_HALF, PUFO_SPLASH_HALF, nSlot)]
            = (u32)pWeapon->uFirePower;

        if (nSlot == 4) {
            /* 0x2f408: a direct hit on a unit. No splash walk at all. */
            MapCell *pCell = pCtx->pfnLookup(nTgtX, nTgtY, nLayer, pCtx->pUser);
            pOut->nDamage = Combat_DirectUnitDamage(pCtx, pCell, pWeapon);
            pOut->eMode = eMode;
            pOut->nImpactX = nImpactX;
            pOut->nImpactY = nImpactY;
            pOut->nFovCalls = nFovCalls;
            return;
        }
    }

    Combat_CeilingBreakSweep(pCtx, pHooks, nLayer, nImpactX, nImpactY, apHits,
                             &eMode, &nImpactX, &nImpactY, &nFovCalls);
    Combat_SplashDamageCells(pCtx, nImpactX, nImpactY, nLayer, pWeapon, apHits,
                             eMode);

    if (eMode != SPLASH_MODE_NORMAL) {
        if (apHitsSave != NULL) {
            /* 0x2f784: keep an untouched copy of the impact layer's window. */
            for (size_t i = 0; i < PUFO_SPLASH_DWORDS; ++i) {
                apHitsSave[i] = apHits[i];
            }
        }
        for (i32 nUp = nLayer + 1; nUp < PUFO_MAP_LAYERS; ++nUp) {
            Combat_AttenuateLayer(pCtx, apHits, nLayer, nUp, nImpactX, nImpactY,
                                  eMode);
            Combat_SplashDamageCells(pCtx, nImpactX, nImpactY, nUp, pWeapon,
                                     apHits, eMode);
        }
        for (i32 nDown = nLayer - 1; nDown >= 0; --nDown) {
            Combat_AttenuateLayer(pCtx, apHits, nLayer, nDown + 1, nImpactX,
                                  nImpactY, eMode);
            Combat_SplashDamageCells(pCtx, nImpactX, nImpactY, nDown, pWeapon,
                                     apHits, eMode);
        }
    }

    if (eMode == SPLASH_MODE_THROW || eMode == SPLASH_MODE_SECONDARY) {
        pHooks->pfnAreaFire(pHooks->pUser);
    }

    Combat_DropCellItems(pCtx);

    pOut->eMode = eMode;
    pOut->nImpactX = nImpactX;
    pOut->nImpactY = nImpactY;
    pOut->nFovCalls = nFovCalls;
}
