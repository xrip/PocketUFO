/* pufo_save.c - save/load chunk serialisation.
 *
 * Evidence: see pufo_save.h for the full address list.
 */
#include "game/pufo_save.h"

#include "game/pufo_clock.h"   /* GameTime_Decode, the load tail */
#include "game/pufo_geoscape.h" /* Base_ClearFacilityGridRow, BaseGrid_SetTypeByte, the five Update*Needed */
#include "game/pufo_strateg.h"  /* StrategList_PurgeType5 / FindIdxForShip / AddItem, Geoscape_SpawnMissionSite */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool Save_BuildSlotPath(char *pszOut, size_t cchOut, const char *pszAppDir, i32 nSlot)
{
    int n;

    if (pszOut == NULL || cchOut == 0) {
        return false;
    }
    /* The original is strcat(szAppDir, "sav") + itoa(slot) + strcat(".sav"), with
     * no bounds handling. snprintf keeps the bounds explicit. */
    n = snprintf(pszOut, cchOut, "%s%s%d%s", pszAppDir != NULL ? pszAppDir : "",
                 "sav", (int)nSlot, ".sav");
    return n > 0 && (size_t)n < cchOut;
}

/* One `DatFile_Write` wrapper that fails loudly, so a short write cannot pass as
 * success the way a bare call would. */
static bool W(DatFile *pDat, const void *pBuf, u32 nSize)
{
    return DatFile_Write(pDat, pBuf, nSize);
}

/* The original's abs is the ARM MOVS/ASR sign-flip idiom; the port spells it the plain way. */
static i32 Abs32(i32 n)
{
    return n < 0 ? -n : n;
}

/* The flags are stored as six separate booleans, one per bit, NOT as a byte. That
 * is three times the size of the flag byte and is reproduced because the read
 * side consumes six bytes too. */
static bool WriteFlags(DatFile *pDat, u8 bFlags)
{
    for (u32 i = 0; i < 6; ++i) {
        u8 bBit = (u8)((bFlags >> i) & 1u);

        if (!W(pDat, &bBit, 1)) {
            return false;
        }
    }
    return true;
}

static bool ReadFlags(DatFile *pDat, u8 *pbFlags)
{
    u8 bFlags = 0;

    for (u32 i = 0; i < 6; ++i) {
        u8 bBit = 0;

        if (!DatFile_Read(pDat, &bBit, 1)) {
            return false;
        }
        bFlags = (u8)(bFlags | ((bBit != 0 ? 1u : 0u) << i));
    }
    *pbFlags = bFlags;
    return true;
}

/* One slot of the tactical inventory block: the item's own id (TactItem+0x14), or the 0x7FFF
 * "nothing here" value. The original reaches the id by `ADD R1, R0, #0x14` on the item pointer and
 * swaps in a stack 0x7FFF when the slot is NULL (0x429e4..0x429f0 and its seven copies). */
static bool WriteItemId(DatFile *pDat, const TactItem *pItem)
{
    u32 nValue = (pItem != NULL) ? (u32)pItem->nRefId : PUFO_SAVE_ID_NONE;

    return W(pDat, &nValue, 4);
}

bool Save_WriteSoldier(DatFile *pDat, const Soldier *pUnit, u8 bTactFlag)
{
    u8 aPad[PUFO_SAVE_SOLDIER_PREFIX];
    u32 nOff = 0;
    u8  bZero = 0;
    u8  aCell[3];
    i32 nLook;
    i32 nFemale;
    u16 uZero = 0;
    u32 nZero32 = 0;
    i32 i;

    if (pDat == NULL || pUnit == NULL) {
        return false;
    }
    memset(aPad, 0, sizeof(aPad));

    /* The six flag bits. */
    if (!WriteFlags(pDat, pUnit->bFlags)) {
        return false;
    }
    (void)nOff;

    /* Scalar fields, in the original's write order. */
    if (!W(pDat, &pUnit->bFacing, 1)) { return false; }
    if (!W(pDat, &pUnit->uTu, 2)) { return false; }
    if (!W(pDat, &pUnit->uStunOrArmor, 2)) { return false; }
    if (!W(pDat, &pUnit->uHp, 2)) { return false; }
    if (!W(pDat, &pUnit->bMorale, 1)) { return false; }

    /* The cell coordinates, or three zero bytes when the unit is not on the map.
     * The original writes the cell's own nX/nY/nZ when `bInTact != 0 && pCell`,
     * else a shared zero dword's first three bytes. */
    if (pUnit->bInTact != 0 && pUnit->pCell != NULL) {
        aCell[0] = pUnit->pCell->nX;
        aCell[1] = pUnit->pCell->nY;
        aCell[2] = pUnit->pCell->nZ;
    } else {
        aCell[0] = 0;
        aCell[1] = 0;
        aCell[2] = 0;
    }
    if (!W(pDat, aCell, 3)) { return false; }

    if (!W(pDat, &pUnit->nAnimPose, 1)) { return false; }
    /* A one-byte zero pad before the draw offsets. */
    if (!W(pDat, &bZero, 1)) { return false; }
    if (!W(pDat, &pUnit->nDrawOffX, 1)) { return false; }
    if (!W(pDat, &pUnit->nDrawOffY, 1)) { return false; }
    if (!W(pDat, &pUnit->bStatus, 1)) { return false; }
    if (!W(pDat, &pUnit->uTuSpent, 2)) { return false; }
    if (!W(pDat, &pUnit->nMultiTarget, 1)) { return false; }

    /* The multi-target block, 120 bytes on both arms. A soldier flagged into the mission writes 30
     * save ids: each one is that target's `nSaveId`, but only while the game itself is in tactical
     * mode -- the writer tests `LOBYTE(Map) @ 0x1139B8`, the same byte the bInTact scan at 0x423A0
     * tests -- and only for the first `nMultiTarget` entries; everything else is 0x7FFF. An
     * unflagged soldier writes the raw buffer the original builds with `memset(buf, 0x7FFF, 0x78)`,
     * and memset takes an int but stores its LOW BYTE, so it is 120 bytes of 0xFF, not a 0x7FFF
     * pattern. Deviation: the original dereferences `apMultiTargets[i]` with no null test; the port
     * writes 0x7FFF for a NULL entry. */
    if (pUnit->bInTact != 0) {
        for (i = 0; i < PUFO_UNIT_MULTITARGET_MAX; ++i) {
            const Soldier *pTarget = pUnit->multiOrAnim.apMultiTargets[i];
            u32            nValue  = PUFO_SAVE_ID_NONE;

            if (bTactFlag != 0 && i < (i32)pUnit->nMultiTarget && pTarget != NULL) {
                nValue = (u32)pTarget->nSaveId;
            }
            if (!W(pDat, &nValue, 4)) {
                return false;
            }
        }
    } else {
        u8 aMulti[120];

        memset(aMulti, 0xFF, sizeof(aMulti));
        if (!W(pDat, aMulti, sizeof(aMulti))) {
            return false;
        }
    }

    if (!W(pDat, &pUnit->bUnk148, 1)) { return false; }
    if (!W(pDat, &pUnit->bPsiAssigned, 1)) { return false; }
    nLook = pUnit->nLook;
    if (!W(pDat, &nLook, 4)) { return false; }
    nFemale = pUnit->bFemale;
    if (!W(pDat, &nFemale, 4)) { return false; }
    if (!W(pDat, &pUnit->nUnitType, 1)) { return false; }

    /* auStat: ten u16. The original writes them one at a time; the IDB renders
     * some indices as `offsetof(Soldier, X)`, which evaluates to 4, 5, 6 and 8 --
     * i.e. ascending 0..9. A single 20-byte write is the same bytes. */
    if (!W(pDat, pUnit->auStat, sizeof(pUnit->auStat))) { return false; }

    if (!W(pDat, &pUnit->nKind, 1)) { return false; }
    if (!W(pDat, &pUnit->bUnk183, 1)) { return false; }
    if (!W(pDat, &pUnit->uMissions, 2)) { return false; }
    if (!W(pDat, pUnit->szName, sizeof(pUnit->szName))) { return false; }
    if (!W(pDat, &pUnit->bRankType, 1)) { return false; }
    /* auStatBase: nine u16, 18 bytes. */
    if (!W(pDat, pUnit->auStatBase, sizeof(pUnit->auStatBase))) { return false; }

    if (!W(pDat, &pUnit->bKneelOrPrimed, 1)) { return false; }
    if (!W(pDat, &pUnit->bUnk247, 1)) { return false; }
    if (!W(pDat, &pUnit->bUnk248, 1)) { return false; }
    if (!W(pDat, &pUnit->uKills, 2)) { return false; }
    if (!W(pDat, pUnit->abReady252, sizeof(pUnit->abReady252))) { return false; }
    if (!W(pDat, &pUnit->bUnk961, 1)) { return false; }
    if (!W(pDat, pUnit->auUnk962, sizeof(pUnit->auUnk962))) { return false; }
    if (!W(pDat, pUnit->abUnk1012, sizeof(pUnit->abUnk1012))) { return false; }

    /* The inventory block, 150 bytes on both arms. A flagged soldier writes 27 item ids (108 bytes)
     * followed by 42 meta bytes; an unflagged one writes the 150 zero bytes the original builds at
     * 0x42C20 (`new(0x96); memset(0); write`). The 27 slots, in the original's order: pHandL (0x429dc,
     * Soldier+0x104), pHandR (0x429f8, +0x108), the four pocket pairs (loop bases 0x42a1c +0x10C,
     * 0x42a50 +0x118, 0x42a84 +0x124, 0x42ab8 +0x130 -- two ids each, the 12-byte pocket stride), the
     * eight backpack slots (the `n += 4 / ii < 4` loop at 0x42aec, +0x13C), the nine belt slots (the
     * 3 x 3 loop at 0x42b58, +0x16C). Then the metas: the four pocket abMeta pairs at 0x42b9c..0x42bd0
     * (+0x114, +0x120, +0x12C, +0x138), 8 + 8 backpack bytes (0x42bdc, 0x42bec), 9 + 9 belt bytes
     * (0x42bfc, 0x42c14).
     *
     * Two backpack slots are never saved: the writer's tests at 0x42b00..0x42b18 (`ii == 1 && n == 4`,
     * `ii == 2 && n == 4`) write 0x7FFF for apBackpack[5] and apBackpack[6] whatever they hold, and the
     * reader clears exactly those two (see Save_ReadSoldier). */
    if (pUnit->bInTact != 0) {
        if (!WriteItemId(pDat, pUnit->pHandL)) { return false; }
        if (!WriteItemId(pDat, pUnit->pHandR)) { return false; }
        for (i = 0; i < PUFO_UNIT_POCKETS; ++i) {
            i32 n;

            for (n = 0; n < 2; ++n) {
                if (!WriteItemId(pDat, pUnit->aPockets[i].apItem[n])) { return false; }
            }
        }
        for (i = 0; i < PUFO_UNIT_BACKPACK_SLOTS; ++i) {
            const TactItem *pSlot = (i == 5 || i == 6) ? NULL : pUnit->apBackpack[i];

            if (!WriteItemId(pDat, pSlot)) { return false; }
        }
        for (i = 0; i < PUFO_UNIT_BELT_SLOTS; ++i) {
            if (!WriteItemId(pDat, pUnit->apBelt[i])) { return false; }
        }
        for (i = 0; i < PUFO_UNIT_POCKETS; ++i) {
            if (!W(pDat, pUnit->aPockets[i].abMeta, 2)) { return false; }
        }
        if (!W(pDat, pUnit->abBackpackMeta, 8)) { return false; }
        if (!W(pDat, &pUnit->abBackpackMeta[8], 8)) { return false; }
        if (!W(pDat, pUnit->abBeltMetaA, 9)) { return false; }
        if (!W(pDat, pUnit->abBeltMetaB, 9)) { return false; }
    } else {
        u8 aInv[150];

        memset(aInv, 0, sizeof(aInv));
        if (!W(pDat, aInv, sizeof(aInv))) {
            return false;
        }
    }

    if (!W(pDat, &pUnit->bHumanoidInv, 1)) { return false; }
    if (!W(pDat, &pUnit->uInvBurden, 2)) { return false; }
    if (!W(pDat, &pUnit->bPsiActive, 1)) { return false; }
    if (!W(pDat, &pUnit->nArmorPoints, 4)) { return false; }
    if (!W(pDat, &pUnit->nClass, 4)) { return false; }
    if (!W(pDat, &pUnit->nUnk436, 4)) { return false; }
    if (!W(pDat, &pUnit->nUnk440, 4)) { return false; }
    if (!W(pDat, &pUnit->nPsiTrainDays, 1)) { return false; }

    /* The spotted block, 244 bytes on both arms: a flagged soldier writes its `nSpottedCount` dword
     * and then 60 save ids (4 + 240 = 244), each target's `nSaveId` when the index is below the count
     * and the pointer is set, 0x7FFF otherwise; an unflagged one writes the 244 zero bytes the
     * original builds with `new(0xF4); memset(0); write`. The reader's half is 0x42214. */
    if (pUnit->bInTact != 0) {
        u32 nCount = (u32)pUnit->nSpottedCount;

        if (!W(pDat, &nCount, 4)) {
            return false;
        }
        for (i = 0; i < PUFO_UNIT_SPOTTED_MAX; ++i) {
            const Soldier *pSpotted = pUnit->apSpotted[i];
            u32            nValue   = PUFO_SAVE_ID_NONE;

            if (i < pUnit->nSpottedCount && pSpotted != NULL) {
                nValue = (u32)pSpotted->nSaveId;
            }
            if (!W(pDat, &nValue, 4)) {
                return false;
            }
        }
    } else {
        u8 aSpotted[244];

        memset(aSpotted, 0, sizeof(aSpotted));
        if (!W(pDat, aSpotted, sizeof(aSpotted))) {
            return false;
        }
    }

    /* Account for the scratch bytes so an unused-variable warning cannot mask a
     * future gap: everything above is written explicitly, nothing comes from aPad. */
    (void)uZero;
    (void)nZero32;
    (void)i;
    return true;
}

/* One slot of the tactical inventory block, read back. 0x7FFF is "nothing here"; 32000 (0x7D00) is
 * the empty-item record itself -- `unk_138EA0 @ Map+0x254E8`, whose nRefId the mission bring-up sets
 * to that value (pufo_tactinit.c) so a slot pointing at it round-trips. Any other value indexes the
 * tactical item pool `Map.apTactItems @ Map+0x23438`. Deviation: the original indexes it with no
 * bound at all, so an out-of-range id reads whatever sits past the pool; the port gives NULL. */
static bool ReadItemId(DatFile *pDat, const SoldierRecordEnv *pEnv, TactItem **ppItem)
{
    u32 nValue = 0;

    if (!DatFile_Read(pDat, &nValue, 4)) {
        return false;
    }
    if (nValue == PUFO_SAVE_ID_NONE) {
        *ppItem = NULL;
    } else if (nValue == PUFO_SAVE_EMPTY_ITEM_ID) {
        *ppItem = (pEnv != NULL) ? pEnv->pEmptyItem : NULL;
    } else if (pEnv != NULL && pEnv->apTactItems != NULL && nValue < (u32)pEnv->nTactItemCount) {
        *ppItem = pEnv->apTactItems[nValue];
    } else {
        *ppItem = NULL;
    }
    return true;
}

bool Save_ReadSoldier(DatFile *pDat, Soldier *pUnit, const SoldierRecordEnv *pEnv)
{
    u8  aSkip[244];
    u8  aCell[3];
    u8  bZero = 0;
    u8  bInTact;
    i32 nLook;
    i32 nFemale;
    u16 uDiscard16;
    i32 nDiscard32;
    i32 i;

    if (pDat == NULL || pUnit == NULL) {
        return false;
    }
    /* `bInTact` comes from the `stactflg` chunk, which the original reads BEFORE the bodies (0x416D0)
     * exactly because the body's variable arms and the cell resolve test it. The memset below is the
     * port's own clean-record step, so the flag has to survive it. */
    bInTact = pUnit->bInTact;
    memset(pUnit, 0, sizeof(*pUnit));
    pUnit->bInTact = bInTact;

    if (!ReadFlags(pDat, &pUnit->bFlags)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bFacing, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->uTu, 2)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->uStunOrArmor, 2)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->uHp, 2)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bMorale, 1)) { return false; }
    /* 0x41968..0x419DC: a soldier flagged into the mission gets its cell POINTER back --
     * apCells[3600*layer + 60*y + x] off the cell table -- and a soldier not flagged has the
     * same three bytes read and dropped. The pointer is what every unit-side reader walks
     * (Tact_SelectUnit's cursor write @ 0x27014, Unit_UpdateVisibility, the enemy AI distance
     * test), so dropping it leaves the reloaded roster with no place to stand. Deviation: a NULL
     * pfnCell drops the bytes -- the geoscape load path has no cell table to resolve against,
     * which is what the original's own unallocated slots amount to there. */
    if (!DatFile_Read(pDat, aCell, 3)) { return false; }
    pUnit->pCell = (pUnit->bInTact != 0 && pEnv != NULL && pEnv->pfnCell != NULL)
                       ? pEnv->pfnCell((i32)aCell[0], (i32)aCell[1], (i32)aCell[2], pEnv->pCellUser)
                       : NULL;
    if (!DatFile_Read(pDat, &pUnit->nAnimPose, 1)) { return false; }
    if (!DatFile_Read(pDat, &bZero, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nDrawOffX, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nDrawOffY, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bStatus, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->uTuSpent, 2)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nMultiTarget, 1)) { return false; }

    /* The multi-target block (0x41A70), 120 bytes on both arms. A flagged soldier's 30 dwords resolve
     * into `multiOrAnim.apMultiTargets`: the original reads each one and keeps it only when the index
     * is below `nMultiTarget` AND the value is below the roster count it read at 0x417D4; anything
     * else, including 0x7FFF, becomes NULL. An unflagged soldier has the same 120 bytes read and
     * dropped. */
    if (pUnit->bInTact != 0) {
        for (i = 0; i < PUFO_UNIT_MULTITARGET_MAX; ++i) {
            u32 nValue = 0;

            if (!DatFile_Read(pDat, &nValue, 4)) { return false; }
            pUnit->multiOrAnim.apMultiTargets[i] = NULL;
            /* The original's own bound is SIGNED: `CMP R6,R3` / `MOVLT` / `CMPLT R0,R3,LSR#16` at
             * 0x41A90..0x41AB8, with nMultiTarget loaded by LDRSB. Deviation: the port reads the id as
             * unsigned, so a 0xFFFFFFFF can never index the roster out of bounds; the original's signed
             * compare would take it as -1. That value never reaches this arm in a file the original
             * itself wrote -- its flagged arm stores only real ids or 0x7FFF, and 0x7FFF fails the bound. */
            if (i < (i32)pUnit->nMultiTarget && nValue != PUFO_SAVE_ID_NONE && pEnv != NULL
                && pEnv->apSoldiers != NULL && nValue < (u32)pEnv->nSoldierCount) {
                pUnit->multiOrAnim.apMultiTargets[i] = pEnv->apSoldiers[nValue];
            }
        }
    } else if (!DatFile_Read(pDat, aSkip, 120)) { return false; }

    if (!DatFile_Read(pDat, &pUnit->bUnk148, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bPsiAssigned, 1)) { return false; }
    if (!DatFile_Read(pDat, &nLook, 4)) { return false; }
    pUnit->nLook = nLook;
    if (!DatFile_Read(pDat, &nFemale, 4)) { return false; }
    pUnit->bFemale = nFemale;
    if (!DatFile_Read(pDat, &pUnit->nUnitType, 1)) { return false; }
    if (!DatFile_Read(pDat, pUnit->auStat, sizeof(pUnit->auStat))) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nKind, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bUnk183, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->uMissions, 2)) { return false; }
    if (!DatFile_Read(pDat, pUnit->szName, sizeof(pUnit->szName))) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bRankType, 1)) { return false; }
    if (!DatFile_Read(pDat, pUnit->auStatBase, sizeof(pUnit->auStatBase))) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bKneelOrPrimed, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bUnk247, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bUnk248, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->uKills, 2)) { return false; }
    if (!DatFile_Read(pDat, pUnit->abReady252, sizeof(pUnit->abReady252))) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bUnk961, 1)) { return false; }
    if (!DatFile_Read(pDat, pUnit->auUnk962, sizeof(pUnit->auUnk962))) { return false; }
    if (!DatFile_Read(pDat, pUnit->abUnk1012, sizeof(pUnit->abUnk1012))) { return false; }

    /* The inventory block (0x41D60), 150 bytes on both arms, read in the same 27-slot + 42-meta order
     * the writer used. The two middle backpack slots resolve to NULL whatever the record holds --
     * the reader's own test (`v38 == 0x7FFF || (m == 1 && k == 1) || (m == 2 && k == 1)`), the mirror
     * of the writer forcing those two to 0x7FFF. */
    if (pUnit->bInTact != 0) {
        i32 n;

        if (!ReadItemId(pDat, pEnv, &pUnit->pHandL)) { return false; }
        if (!ReadItemId(pDat, pEnv, &pUnit->pHandR)) { return false; }
        for (i = 0; i < PUFO_UNIT_POCKETS; ++i) {
            for (n = 0; n < 2; ++n) {
                if (!ReadItemId(pDat, pEnv, &pUnit->aPockets[i].apItem[n])) { return false; }
            }
        }
        for (i = 0; i < PUFO_UNIT_BACKPACK_SLOTS; ++i) {
            TactItem *pItem = NULL;

            if (!ReadItemId(pDat, pEnv, &pItem)) { return false; }
            pUnit->apBackpack[i] = (i == 5 || i == 6) ? NULL : pItem;
        }
        for (i = 0; i < PUFO_UNIT_BELT_SLOTS; ++i) {
            if (!ReadItemId(pDat, pEnv, &pUnit->apBelt[i])) { return false; }
        }
        for (i = 0; i < PUFO_UNIT_POCKETS; ++i) {
            if (!DatFile_Read(pDat, pUnit->aPockets[i].abMeta, 2)) { return false; }
        }
        if (!DatFile_Read(pDat, pUnit->abBackpackMeta, 8)) { return false; }
        if (!DatFile_Read(pDat, &pUnit->abBackpackMeta[8], 8)) { return false; }
        if (!DatFile_Read(pDat, pUnit->abBeltMetaA, 9)) { return false; }
        if (!DatFile_Read(pDat, pUnit->abBeltMetaB, 9)) { return false; }
    } else if (!DatFile_Read(pDat, aSkip, 150)) { return false; }

    if (!DatFile_Read(pDat, &pUnit->bHumanoidInv, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->uInvBurden, 2)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bPsiActive, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nArmorPoints, 4)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nClass, 4)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nUnk436, 4)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nUnk440, 4)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nPsiTrainDays, 1)) { return false; }

    /* The spotted block (0x42214), 244 bytes on both arms: the count dword and 60 ids resolved against
     * the roster the same way the cell link is resolved, 0x7FFF becoming NULL. Deviation: the original
     * indexes `Map.apSoldiers` with no bound here; the port bounds against the roster count. */
    if (pUnit->bInTact != 0) {
        if (!DatFile_Read(pDat, &pUnit->nSpottedCount, 4)) { return false; }
        for (i = 0; i < PUFO_UNIT_SPOTTED_MAX; ++i) {
            u32 nValue = 0;

            if (!DatFile_Read(pDat, &nValue, 4)) { return false; }
            pUnit->apSpotted[i] = NULL;
            if (nValue != PUFO_SAVE_ID_NONE && pEnv != NULL && pEnv->apSoldiers != NULL
                && (i32)nValue < pEnv->nSoldierCount) {
                pUnit->apSpotted[i] = pEnv->apSoldiers[nValue];
            }
        }
    } else if (!DatFile_Read(pDat, aSkip, 244)) { return false; }

    /* 0x4229C: the reader defaults auStat[8] (nStatThrow) to 10 when the record carries 0,
     * so a saved zero never reaches the unit. */
    if (pUnit->auStat[8] == 0) {
        pUnit->auStat[8] = 10;
    }

    (void)nDiscard32;
    (void)uDiscard16;
    return true;
}

/* The two trailing chunks: `stactflg` holds one byte per soldier (bInTact), and
 * `1_24scb` one u32 per soldier (the save id of the linked unit, or 0x7FFF). */
bool Save_WriteTrailingChunks(DatFile *pDat, Soldier *const *apSoldiers, i32 nCount)
{
    if (pDat == NULL || apSoldiers == NULL || nCount < 0) {
        return false;
    }
    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_STACTFLG)) {
        return false;
    }
    for (i32 i = 0; i < nCount; ++i) {
        u8 bValue = (u8)(apSoldiers[i] != NULL && apSoldiers[i]->bInTact != 0 ? 1 : 0);

        if (!W(pDat, &bValue, 1)) {
            return false;
        }
    }

    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_124SCB)) {
        return false;
    }
    for (i32 i = 0; i < nCount; ++i) {
        u32 nValue = 0x7FFFu; /* the "no link" sentinel */

        if (apSoldiers[i] != NULL && apSoldiers[i]->pLinked1040 != NULL) {
            /* 0x42E64: `LDR R3,[R0,#0x3BC]` -- the linked soldier's nSaveId. The original reaches it
             * by offset into its own 1044-byte Soldier; the port's Soldier is host-sized, so the
             * field is named. (Reading 0x3BC off the host layout would have written whatever dword
             * happened to sit there -- this is the only +offset reach into Soldier in the module.) */
            nValue = (u32)((const Soldier *)apSoldiers[i]->pLinked1040)->nSaveId;
        }
        if (!W(pDat, &nValue, 4)) {
            return false;
        }
    }
    return true;
}

bool Save_ReadInTactFlags(DatFile *pDat, Soldier *const *apSoldiers, i32 nCount, u8 bFallback)
{
    if (pDat == NULL || apSoldiers == NULL || nCount < 0) {
        return false;
    }

    /* 0x416D0/0x41704: the chunk is optional. Present, each soldier takes one byte stored as
     * `pBuf == 1` (0x41744) -- any value other than 1 lands as 0 rather than the byte itself.
     * Absent, the original fills every entry from LOBYTE(Map) -- the tact flag -- writing 1 or 0
     * for each (0x41790 and 0x4179C), so the caller's fallback replaces the byte, not a no-op. */
    (void)DatFile_FindChunk(pDat, PUFO_SAVE_TAG_STACTFLG);
    if (!DatFile_ChunkPresent(pDat)) {
        for (i32 i = 0; i < nCount; ++i) {
            if (apSoldiers[i] != NULL) {
                apSoldiers[i]->bInTact = (bFallback != 0) ? 1u : 0u;
            }
        }
        return true;
    }

    for (i32 i = 0; i < nCount; ++i) {
        u8 bValue = 0;

        if (!DatFile_Read(pDat, &bValue, 1)) {
            return false;
        }
        if (apSoldiers[i] != NULL) {
            apSoldiers[i]->bInTact = (bValue == 1) ? 1u : 0u;
        }
    }
    return true;
}

bool Save_ReadLinkedSoldiers(DatFile *pDat, Soldier *const *apSoldiers, i32 nCount)
{
    if (pDat == NULL || apSoldiers == NULL || nCount < 0) {
        return false;
    }

    /* 0x422C4..0x42340: one u32 per soldier, resolved as an INDEX into apSoldiers (0x42320) -- the
     * writer stored the linked soldier's nSaveId (0x42E64), and a soldier's nSaveId is its roster
     * index (Save_AllocSoldiers, pufo_save.c:652), which is why the two agree. 0x7FFF means no
     * link. The original indexes with no bound; the port bounds it. */
    (void)DatFile_FindChunk(pDat, PUFO_SAVE_TAG_124SCB);
    if (!DatFile_ChunkPresent(pDat)) {
        return true;
    }

    for (i32 i = 0; i < nCount; ++i) {
        u32 nValue = 0;

        if (!DatFile_Read(pDat, &nValue, 4)) {
            return false;
        }
        if (apSoldiers[i] != NULL) {
            apSoldiers[i]->pLinked1040 = (nValue != 0x7FFFu && (i32)nValue < nCount)
                                             ? apSoldiers[nValue] : NULL;
        }
    }
    return true;
}

bool Save_WriteVersion(DatFile *pDat)
{
    u16 nVer = (u16)PUFO_SAVE_TACTVER_VALUE;

    if (pDat == NULL) {
        return false;
    }
    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_TACTVER)) {
        return false;
    }
    return W(pDat, &nVer, 2);
}

bool Save_ReadVersion(DatFile *pDat, u8 *pnOutVer)
{
    u16 nVer = 0;

    if (pDat == NULL) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_TACTVER)) {
        return false;
    }
    if (!DatFile_Read(pDat, &nVer, 2)) {
        return false;
    }
    if (pnOutVer != NULL) {
        *pnOutVer = (u8)nVer;
    }
    return true;
}

/* Save_ReadStrategVer @ 0x3f1c8.
 *
 * FindChunk("strateg") then read one byte into the save format version byte at 0x18655C.
 * LoadGame @ 0x44d84 calls this before reading any chunk body, so the version that decides how
 * the other chunks are sized comes from the strateg chunk.
 *
 * The original checks neither result: FindChunk's return is dropped, so a save with no strateg
 * chunk still runs the read at whatever position FindChunk left, and the previous version byte
 * survives a failed read. Reproduced. */
bool Save_ReadStrategVersion(DatFile *pDat, i8 *pbVersion)
{
    if (pDat == NULL || pbVersion == NULL) {
        return false;
    }
    (void)DatFile_FindChunk(pDat, PUFO_SAVE_TAG_STRATEG);
    return DatFile_Read(pDat, pbVersion, 1);
}

/* 0x423A0..0x424C8: the writer COMPUTES `bInTact`; nothing in the game sets that field. Tact flag
 * clear (LOBYTE(Map), tested at 0x423B0) -- every soldier is flagged out. Tact flag set -- the default
 * is 1 (`MOV R5,#1` at 0x423C0) and the four roster scans (apXcomUnits 0x15154 @ 0x423D4, apEnemyUnits
 * 0x151CC @ 0x42414, apCivilians 0x15244 @ 0x42454, the hidden park unk_138DF0 @ 0x42494) clear it
 * again for a soldier in none of them. The result is stored INTO the soldier (`STRB R5,[R4,#0x40E]` at
 * 0x424C8) before its record is written, because the record's own cell arm reads the field. */
static u8 Save_bSoldierInTact(const Soldier *pUnit, u8 bTactFlag, const TeamUnitsState *pRosters)
{
    if (pUnit == NULL || bTactFlag == 0 || pRosters == NULL) {
        return 0;   /* no tact map, or no rosters to scan -- and the scans are what clear the default */
    }

    for (i32 i = 0; i < (i32)pRosters->nXcomCount; ++i) {
        if (pRosters->apXcomUnits != NULL && pRosters->apXcomUnits[i] == pUnit) { return 1; }
    }
    for (i32 i = 0; i < (i32)pRosters->nEnemyCount; ++i) {
        if (pRosters->apEnemyUnits != NULL && pRosters->apEnemyUnits[i] == pUnit) { return 1; }
    }
    for (i32 i = 0; i < pRosters->nCivilianCount; ++i) {
        if (pRosters->apCivilians != NULL && pRosters->apCivilians[i] == pUnit) { return 1; }
    }
    for (i32 i = 0; i < pRosters->nHiddenCount; ++i) {
        if (pRosters->apHiddenUnits != NULL && pRosters->apHiddenUnits[i] == pUnit) { return 1; }
    }
    return 0;
}

/* Save_WriteSoldiers @ 0x42350.
 *
 * The order is: begin the chunk, write the u16 count, every record, then the two
 * trailing per-soldier chunks. This is the only valid entry point because
 * DatFile_Write refuses to work without an open chunk (verified: pufo_dat.c checks
 * `nCurEntry == 0` and returns false). */
bool Save_WriteSoldiers(DatFile *pDat, Soldier *const *apSoldiers, i32 nCount,
                        u8 bTactFlag, const TeamUnitsState *pRosters)
{
    u16 uCount = (u16)nCount;

    if (pDat == NULL) {
        return false;
    }
    if (nCount < 0) {
        return false;
    }
    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_SOLDIERS)) {
        return false;
    }
    if (!W(pDat, &uCount, 2)) {
        return false;
    }
    if (apSoldiers != NULL) {
        for (i32 i = 0; i < nCount; ++i) {
            if (apSoldiers[i] == NULL) {
                continue;
            }
            apSoldiers[i]->bInTact = Save_bSoldierInTact(apSoldiers[i], bTactFlag, pRosters);
            if (!Save_WriteSoldier(pDat, apSoldiers[i], bTactFlag)) {
                return false;
            }
        }
    }
    return Save_WriteTrailingChunks(pDat, apSoldiers, nCount);
}

/* Save_ReadSoldiers @ 0x416bc. The original allocates the roster from the count;
 * this port fills caller-provided storage so ownership stays explicit.
 *
 * The chunk order is load-bearing, not cosmetic. `stactflg` runs at 0x416D0, BEFORE the bodies at
 * 0x417BC, because each body tests bInTact: the cell arm (0x41968 -> the 0x419DC pointer write),
 * the 30-entry multi-target arm (0x41A70), the inventory arm (0x41D60) and the spotted arm (0x42214).
 * `1_24scb` runs at 0x422C4, after them. The two chunks are addressed by tag (DatFile_FindChunk
 * re-seeks), so this reader may take the count from the soldiers chunk first -- the original takes it
 * from the same two bytes in Soldiers_Reload (0x430F8) -- and then re-finds both. */
bool Save_ReadSoldiers(DatFile *pDat, Soldier **apapSoldiers, i32 *pnCount,
                       i32 nCapacity, const SoldierRecordEnv *pEnv, u8 bInTactFallback)
{
    SoldierRecordEnv arms;
    u16 uCount = 0;

    if (pDat == NULL || pnCount == NULL) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_SOLDIERS)) {
        return false;
    }
    if (!DatFile_Read(pDat, &uCount, 2)) {
        return false;
    }
    if ((i32)uCount > nCapacity) {
        return false;
    }

    if (!Save_ReadInTactFlags(pDat, apapSoldiers, (i32)uCount, bInTactFallback)) {
        return false;
    }

    /* 0x417BC re-finds the chunk and 0x417D4 reads the count again -- the re-find rewinds to the
     * chunk start, so skipping that second 2-byte read would put every body two bytes early. The
     * original replaces its count here; the port takes the second value for the bodies too. */
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_SOLDIERS)) {
        return false;
    }
    if (!DatFile_Read(pDat, &uCount, 2)) {
        return false;
    }
    if ((i32)uCount > nCapacity) {
        return false;
    }
    /* The three bInTact arms resolve their ids against the roster and the tactical item pool. The
     * bound is the count this reader just re-read at 0x417D4, not whatever the caller held before, so
     * the env copy carries it. */
    arms = (pEnv != NULL) ? *pEnv : (SoldierRecordEnv){ 0 };
    arms.apSoldiers   = apapSoldiers;
    arms.nSoldierCount = (i32)uCount;

    for (i32 i = 0; i < (i32)uCount; ++i) {
        if (apapSoldiers != NULL && apapSoldiers[i] != NULL) {
            if (!Save_ReadSoldier(pDat, apapSoldiers[i], &arms)) {
                return false;
            }
        }
    }

    if (!Save_ReadLinkedSoldiers(pDat, apapSoldiers, (i32)uCount)) {
        return false;
    }
    *pnCount = (i32)uCount;
    return true;
}

/* Soldiers_Reload @ 0x430cc.
 *
 * It reads only the 2-byte count from the "soldiers" chunk. The bodies come from Save_ReadSoldiers, which
 * runs after this, so this is the allocation half of a load: afterwards the roster is the saved size and
 * every entry is a freshly constructed empty soldier.
 *
 * Two things the original does that are worth naming. It frees with operator delete and never nulls the
 * slots it freed, so a stale count would make a later pass free the same memory twice; the port nulls them.
 * And it stores the RESULT of Soldier_ctor rather than the pointer it allocated - harmless here because
 * Soldier_ctor returns its argument, but it is why the code looks like a type error. */
bool Soldiers_Reload(DatFile *pDat, Soldier **apSoldiers, i32 *pnCount, i32 nCapacity)
{
    u16 uCount = 0;

    if (pDat == NULL || apSoldiers == NULL || pnCount == NULL) {
        return false;
    }

    /* 0x430D8: free the roster as it stands before the chunk is even looked at. */
    for (i32 i = 0; i < *pnCount; ++i) {
        free(apSoldiers[i]);
        apSoldiers[i] = NULL;
    }

    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_SOLDIERS)) {
        return false;
    }
    if (!DatFile_Read(pDat, &uCount, 2)) {
        return false;
    }

    /* Deviation: the original writes straight into apSoldiers with no bound on the count it just read. */
    if ((i32)uCount > nCapacity) {
        uCount = (u16)nCapacity;
    }

    for (i32 i = 0; i < (i32)uCount; ++i) {
        Soldier *pUnit = (Soldier *)malloc(sizeof(Soldier));
        if (pUnit != NULL) {
            Unit_ctor(pUnit);
        }
        apSoldiers[i] = pUnit;
    }

    *pnCount = (i32)uCount;
    return true;
}

/* ------------------------------------------------------------ the small chunks */

bool Save_WriteGameState(DatFile *pDat, u8 bTactFlag)
{
    if (pDat == NULL) {
        return false;
    }
    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_GAMESTATE)) {
        return false;
    }
    /* One byte, taken from pCtx[0] in the original -- the tactical flag. */
    return W(pDat, &bTactFlag, 1);
}

bool Save_ReadGameState(DatFile *pDat, u8 *pbOutGameState)
{
    if (pDat == NULL || pbOutGameState == NULL) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_GAMESTATE)) {
        return false;
    }
    /* The original restores into bGameState @ 0x15821C, NOT back into the flag it
     * wrote from. The asymmetry is reproduced by keeping this a separate output. */
    return DatFile_Read(pDat, pbOutGameState, 1);
}

bool Save_WriteObjBefore(DatFile *pDat, ObjectRecFull *const *apObjects, i32 nObjectCount)
{
    if (pDat == NULL || apObjects == NULL || nObjectCount < 0) {
        return false;
    }
    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_OBJBEFORE)) {
        return false;
    }
    /* One u16 per object: the recovery score at 0xCA of the 208-byte record. */
    for (i32 i = 0; i < nObjectCount; ++i) {
        if (apObjects[i] == NULL) {
            return false;
        }
        if (!W(pDat, &apObjects[i]->uRecovScore, 2)) {
            return false;
        }
    }
    return true;
}

bool Save_ReadObjBefore(DatFile *pDat, ObjectRecFull *const *apObjects, i32 nObjectCount)
{
    if (pDat == NULL || apObjects == NULL || nObjectCount < 0) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_OBJBEFORE)) {
        return false;
    }
    for (i32 i = 0; i < nObjectCount; ++i) {
        if (apObjects[i] == NULL) {
            return false;
        }
        if (!DatFile_Read(pDat, &apObjects[i]->uRecovScore, 2)) {
            return false;
        }
    }
    return true;
}

bool Save_WriteTime(DatFile *pDat, const SaveTimeRecord *pTime)
{
    if (pDat == NULL || pTime == NULL) {
        return false;
    }
    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_TIME)) {
        return false;
    }
    /* The write order is the original's, which is NOT the slot order: 105, 106,
     * 103, 102, 101, 114, 115, then the word. */
    if (!W(pDat, &pTime->nSlot105, 4)) { return false; }
    if (!W(pDat, &pTime->nSlot106, 1)) { return false; }
    if (!W(pDat, &pTime->nSlot103, 4)) { return false; }
    if (!W(pDat, &pTime->nSlot102, 4)) { return false; }
    if (!W(pDat, &pTime->nSlot101, 4)) { return false; }
    if (!W(pDat, &pTime->nSlot114, 4)) { return false; }
    if (!W(pDat, &pTime->nSlot115, 4)) { return false; }
    return W(pDat, &pTime->nWord1472C8, 2);
}

bool Save_ReadTime(DatFile *pDat, SaveTimeRecord *pTime)
{
    if (pDat == NULL || pTime == NULL) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_TIME)) {
        return false;
    }
    if (!DatFile_Read(pDat, &pTime->nSlot105, 4)) { return false; }
    if (!DatFile_Read(pDat, &pTime->nSlot106, 1)) { return false; }
    if (!DatFile_Read(pDat, &pTime->nSlot103, 4)) { return false; }
    if (!DatFile_Read(pDat, &pTime->nSlot102, 4)) { return false; }
    if (!DatFile_Read(pDat, &pTime->nSlot101, 4)) { return false; }
    if (!DatFile_Read(pDat, &pTime->nSlot114, 4)) { return false; }
    if (!DatFile_Read(pDat, &pTime->nSlot115, 4)) { return false; }
    return DatFile_Read(pDat, &pTime->nWord1472C8, 2);
}

bool Save_AssignRosterIndices(bool bTactFlag, Soldier **apSoldiers, i32 nSoldierCount,
                              TactItem **apTactItems, i32 nTactItemCount)
{
    /* The whole function is gated on the tactical flag. */
    if (!bTactFlag) {
        return false;
    }

    if (apSoldiers != NULL) {
        for (i32 i = 0; i < nSoldierCount; ++i) {
            if (apSoldiers[i] != NULL) {
                apSoldiers[i]->nSaveId = i;
            }
        }
    }

    /* The item counter RESTARTS at 0 rather than continuing from the soldier count,
     * so the two id spaces deliberately overlap. */
    if (apTactItems != NULL) {
        for (i32 i = 0; i < nTactItemCount; ++i) {
            if (apTactItems[i] != NULL) {
                apTactItems[i]->nRefId = i;
            }
        }
    }
    return true;
}

/* Write one ResearchTopic in the original's field order. */
static bool Save_WriteResearchTopic(DatFile *pDat, const ResearchTopic *pTopic)
{
    return DatFile_Write(pDat, &pTopic->bComplete, 1)
        && DatFile_Write(pDat, &pTopic->bUnk31, 1)
        && DatFile_Write(pDat, &pTopic->wNameId, 2)
        && DatFile_Write(pDat, pTopic->abUnk20, 5)
        && DatFile_Write(pDat, &pTopic->wUnk0, 2)
        && DatFile_Write(pDat, pTopic->abUnk10, 5)
        && DatFile_Write(pDat, &pTopic->bAvailable, 1)
        && DatFile_Write(pDat, &pTopic->bUnk30, 1)
        && DatFile_Write(pDat, &pTopic->bUnk33, 1)
        && DatFile_Write(pDat, pTopic->abUnk25, 5)
        && DatFile_Write(pDat, pTopic->abUnlock, 5)
        && DatFile_Write(pDat, &pTopic->wProgress, 2)
        && DatFile_Write(pDat, &pTopic->bUnk32, 1)
        && DatFile_Write(pDat, &pTopic->wUnk2, 2);
}

static bool Save_ReadResearchTopic(DatFile *pDat, ResearchTopic *pTopic)
{
    return DatFile_Read(pDat, &pTopic->bComplete, 1)
        && DatFile_Read(pDat, &pTopic->bUnk31, 1)
        && DatFile_Read(pDat, &pTopic->wNameId, 2)
        && DatFile_Read(pDat, pTopic->abUnk20, 5)
        && DatFile_Read(pDat, &pTopic->wUnk0, 2)
        && DatFile_Read(pDat, pTopic->abUnk10, 5)
        && DatFile_Read(pDat, &pTopic->bAvailable, 1)
        && DatFile_Read(pDat, &pTopic->bUnk30, 1)
        && DatFile_Read(pDat, &pTopic->bUnk33, 1)
        && DatFile_Read(pDat, pTopic->abUnk25, 5)
        && DatFile_Read(pDat, pTopic->abUnlock, 5)
        && DatFile_Read(pDat, &pTopic->wProgress, 2)
        && DatFile_Read(pDat, &pTopic->bUnk32, 1)
        && DatFile_Read(pDat, &pTopic->wUnk2, 2);
}

/* Save_WriteResearch @ 0x3f558. */
bool Save_WriteResearch(DatFile *pDat, const SaveResearchState *pState)
{
    if (pDat == NULL || pState == NULL) {
        return false;
    }
    if (!DatFile_BeginChunk(pDat, "research")) {
        return false;
    }
    if (!DatFile_Write(pDat, pState->abHeadA, PUFO_RESEARCH_HEAD_A_BYTES)
        || !DatFile_Write(pDat, pState->abHeadB, PUFO_RESEARCH_HEAD_B_BYTES)
        || !DatFile_Write(pDat, pState->abHeadC, PUFO_RESEARCH_HEAD_C_BYTES)
        || !DatFile_Write(pDat, pState->abHeadD, PUFO_RESEARCH_HEAD_D_BYTES)) {
        return false;
    }
    for (i32 i = 0; i < PUFO_RESEARCH_TOPIC_COUNT; ++i) {
        if (!Save_WriteResearchTopic(pDat, &pState->aTopics[i])) {
            return false;
        }
    }
    return true;
}

/* Save_ReadResearch @ 0x406e8. */
bool Save_ReadResearch(DatFile *pDat, SaveResearchState *pState)
{
    if (pDat == NULL || pState == NULL) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, "research")) {
        return false;
    }
    if (!DatFile_Read(pDat, pState->abHeadA, PUFO_RESEARCH_HEAD_A_BYTES)
        || !DatFile_Read(pDat, pState->abHeadB, PUFO_RESEARCH_HEAD_B_BYTES)
        || !DatFile_Read(pDat, pState->abHeadC, PUFO_RESEARCH_HEAD_C_BYTES)
        || !DatFile_Read(pDat, pState->abHeadD, PUFO_RESEARCH_HEAD_D_BYTES)) {
        return false;
    }
    for (i32 i = 0; i < PUFO_RESEARCH_TOPIC_COUNT; ++i) {
        if (!Save_ReadResearchTopic(pDat, &pState->aTopics[i])) {
            return false;
        }
    }
    return true;
}

/* Write one SmapSite in the original's field order (740 bytes on disk). */
static bool Save_WriteSmapSite(DatFile *pDat, const SmapSite *pSite)
{
    return DatFile_Write(pDat, &pSite->bUnk518, 1)
        && DatFile_Write(pDat, &pSite->uRecoveredSoldiers, 2)
        && DatFile_Write(pDat, &pSite->bFlag1, 1)
        && DatFile_Write(pDat, &pSite->bHasLoot, 1)
        && DatFile_Write(pDat, pSite->auItemQty, 0x200)
        && DatFile_Write(pDat, &pSite->bRecoveredCraftCount, 1)
        && DatFile_Write(pDat, &pSite->bRecoveredSoldierAssign, 1)
        && DatFile_Write(pDat, &pSite->bFlag0, 1)
        && DatFile_Write(pDat, pSite->anCraftIds, 0x14)
        && DatFile_Write(pDat, pSite->anSoldierIds, 0xC8);
}

static bool Save_ReadSmapSite(DatFile *pDat, SmapSite *pSite)
{
    return DatFile_Read(pDat, &pSite->bUnk518, 1)
        && DatFile_Read(pDat, &pSite->uRecoveredSoldiers, 2)
        && DatFile_Read(pDat, &pSite->bFlag1, 1)
        && DatFile_Read(pDat, &pSite->bHasLoot, 1)
        && DatFile_Read(pDat, pSite->auItemQty, 0x200)
        && DatFile_Read(pDat, &pSite->bRecoveredCraftCount, 1)
        && DatFile_Read(pDat, &pSite->bRecoveredSoldierAssign, 1)
        && DatFile_Read(pDat, &pSite->bFlag0, 1)
        && DatFile_Read(pDat, pSite->anCraftIds, 0x14)
        && DatFile_Read(pDat, pSite->anSoldierIds, 0xC8);
}

/* Save_WriteSmap @ 0x3fa28. */
bool Save_WriteSmap(DatFile *pDat, const SaveSmapState *pState)
{
    if (pDat == NULL || pState == NULL) {
        return false;
    }
    if (!DatFile_BeginChunk(pDat, "smap")) {
        return false;
    }
    if (!DatFile_Write(pDat, &pState->nCamParam0, 4)
        || !DatFile_Write(pDat, &pState->nCamScrollX, 4)
        || !DatFile_Write(pDat, &pState->nCamScrollY, 4)
        || !DatFile_Write(pDat, &pState->nParam3, 4)
        || !DatFile_Write(pDat, &pState->nParam4, 4)) {
        return false;
    }
    for (i32 i = 0; i < PUFO_SMAP_SITE_COUNT; ++i) {
        if (!Save_WriteSmapSite(pDat, &pState->aSites[i])) {
            return false;
        }
    }
    return DatFile_Write(pDat, pState->abTail5, 5)
        && DatFile_Write(pDat, pState->abTail8a, 8)
        && DatFile_Write(pDat, pState->abTail8b, 8);
}

/* Save_ReadSmap @ 0x40c4c. Not a plain mirror: the original restores the zoom level it
 * just read, unless unk_15821C is set. No scroll rescale and no vertical clamp run here,
 * unlike Smap_ZoomIn @ 0x81e44. */
bool Save_ReadSmap(DatFile *pDat, SaveSmapState *pState, SmapView *pView,
                   const SmapZoomSet aSets[3], SmapWorldSize *pWorld,
                   bool bSkipZoomRestore)
{
    if (pDat == NULL || pState == NULL) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_SMAP)) { /* 0x40C60 */
        return false;
    }
    if (!DatFile_Read(pDat, &pState->nCamParam0, 4)  /* 0x40C78: viewport nMode */
        || !DatFile_Read(pDat, &pState->nCamScrollX, 4) /* 0x40C90 */
        || !DatFile_Read(pDat, &pState->nCamScrollY, 4) /* 0x40CA8 */
        || !DatFile_Read(pDat, &pState->nParam3, 4)     /* 0x40D94 */
        || !DatFile_Read(pDat, &pState->nParam4, 4)) {  /* 0x40DAC */
        return false;
    }

    if (pView != NULL) {
        pView->nMode    = pState->nCamParam0;
        pView->nScrollX = pState->nCamScrollX;
        pView->nScrollY = pState->nCamScrollY;

        /* 0x40CB4: the unk_15821C gate, then the switch on the mode just read. */
        if (!bSkipZoomRestore && aSets != NULL &&
            pState->nCamParam0 >= 0 && pState->nCamParam0 < 3) {
            Smap_ApplyZoomSet(pView, &aSets[pState->nCamParam0], pWorld);
        }
    }

    for (i32 i = 0; i < PUFO_SMAP_SITE_COUNT; ++i) { /* 0x40DB4..0x40E7C */
        if (!Save_ReadSmapSite(pDat, &pState->aSites[i])) {
            return false;
        }
    }

    /* 0x40E84..0x40EB8: unk_161F40, then unk_162000, then unk_161FF0. */
    return DatFile_Read(pDat, pState->abTail5, 5)
        && DatFile_Read(pDat, pState->abTail8a, 8)
        && DatFile_Read(pDat, pState->abTail8b, 8);
}

/* 0x3f4f4. All three writes run; the original returns only the last result (0x3F52C), so a
 * failed middle write does not stop the chunk. The tail region is never written. */
bool Save_WriteProduct(DatFile *pDat, const u8 *pBlock)
{
    if (pDat == NULL || pBlock == NULL) {
        return false;
    }
    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_PRODUCT)) {
        return false; /* 0x3F500 */
    }
    (void)DatFile_Write(pDat, pBlock, 6400);          /* 0x3F514 */
    (void)DatFile_Write(pDat, pBlock + 6400, 6400);   /* 0x3F520 */
    return DatFile_Write(pDat, pBlock + 12800, 3200); /* 0x3F52C */
}

/* 0x4062c. The offsets are fixed at 0 / 6400 / 12800 / 16000 in BOTH branches; only the
 * sizes change. An old save therefore fills entries 0..1151 of each array and leaves the
 * rest of the block untouched. */
bool Save_ReadProduct(DatFile *pDat, i32 nSaveFmtVer, u8 *pBlock)
{
    if (pDat == NULL || pBlock == NULL) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_PRODUCT)) {
        return false; /* 0x40638 */
    }
    if (nSaveFmtVer >= 3) {
        if (!DatFile_Read(pDat, pBlock, 6400)) {
            return false; /* 0x406B0 */
        }
        if (!DatFile_Read(pDat, pBlock + 6400, 6400)) {
            return false; /* 0x406C8 */
        }
        return DatFile_Read(pDat, pBlock + 12800, 3200); /* 0x406D0, array C via the tail */
    }
    if (!DatFile_Read(pDat, pBlock, 4608)) {
        return false; /* 0x4065C */
    }
    if (!DatFile_Read(pDat, pBlock + 6400, 4608)) {
        return false; /* 0x40674 */
    }
    if (!DatFile_Read(pDat, pBlock + 12800, 2304)) {
        return false; /* 0x4068C */
    }
    return DatFile_Read(pDat, pBlock + 16000, 1152); /* 0x40690, the region past array C */
}

/* The facility record, field by field, in the original's order (0x3F31C..0x3F39C).
 * Nine writes for a 61-byte record; gap_1 and gap_46 of the 64-byte in-memory
 * BaseFacility are skipped. */
static bool WriteFacility(DatFile *pDat, const SaveBaseFacility *pFac)
{
    return W(pDat, pFac->szName, 20)
        && W(pDat, &pFac->bType, 1)
        && W(pDat, &pFac->nBuildState, 4)
        && W(pDat, pFac->szUnk6, 20)
        && W(pDat, &pFac->nFacType, 4)
        && W(pDat, &pFac->nUnk48, 4)
        && W(pDat, &pFac->uUnk2, 2)
        && W(pDat, &pFac->uUnk4, 2)
        && W(pDat, &pFac->nDemolishState, 4);
}

static bool ReadFacility(DatFile *pDat, SaveBaseFacility *pFac)
{
    return DatFile_Read(pDat, pFac->szName, 20)
        && DatFile_Read(pDat, &pFac->bType, 1)
        && DatFile_Read(pDat, &pFac->nBuildState, 4)
        && DatFile_Read(pDat, pFac->szUnk6, 20)
        && DatFile_Read(pDat, &pFac->nFacType, 4)
        && DatFile_Read(pDat, &pFac->nUnk48, 4)
        && DatFile_Read(pDat, &pFac->uUnk2, 2)
        && DatFile_Read(pDat, &pFac->uUnk4, 2)
        && DatFile_Read(pDat, &pFac->nDemolishState, 4);
}

bool Save_WriteBases(DatFile *pDat, const SaveBasesState *pState,
                     const BaseRec *const *apBases,
                     ObjectRecFull *const *apObjects, i32 nObjectCount)
{
    if (pDat == NULL || pState == NULL) {
        return false;
    }
    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_BASES)) {
        return false; /* 0x3F208 */
    }

    if (!W(pDat, pState->abBlock288, 288))  { return false; } /* 0x3F21C, 0x159768 */
    if (!W(pDat, pState->abBlock1152, 1152)){ return false; } /* 0x3F22C, 0x1598DC */
    if (!W(pDat, &pState->bBaseCount, 1))   { return false; } /* 0x3F244, 0x159D80 */
    if (!W(pDat, &pState->bUnk159D81, 1))   { return false; } /* 0x3F25C */
    if (!W(pDat, &pState->bUnk159D82, 1))   { return false; } /* 0x3F274 */
    if (!W(pDat, pState->abUnk159D90, 4))   { return false; } /* 0x3F28C */
    if (!W(pDat, pState->abUnk159DBC, 8))   { return false; } /* 0x3F2A4: always 8 written */
    if (!W(pDat, pState->abUnk1598D8, 4))   { return false; } /* 0x3F2B4 */
    if (!W(pDat, &pState->bUnk158848, 1))   { return false; } /* 0x3F2C8: always written */

    /* 0x3F2CC..0x3F2E8: one byte per place flag, bFlag20 of a 28-byte BaseFacFlag. */
    for (i32 i = 0; i < PUFO_BASE_FLAG_COUNT; ++i) {
        if (!W(pDat, &pState->abFlag20[i], 1)) {
            return false;
        }
    }

    if (!W(pDat, pState->aRegionBuildCount, 9)) { return false; } /* 0x3F304 */

    for (i32 f = 0; f < PUFO_BASE_FAC_COUNT; ++f) {
        if (!WriteFacility(pDat, &pState->aFac[f])) {
            return false;
        }
    }

    if ((i8)pState->bBaseCount > 0 && apBases != NULL) {
        for (i32 b = 0; b < (i8)pState->bBaseCount; ++b) {
            const BaseRec *pBase = apBases[b];

            if (pBase == NULL) {
                return false;
            }
            if (!W(pDat, &pBase->bSlot, 1))        { return false; } /* 0x3F3EC */
            if (!W(pDat, &pBase->bUnk4, 1))        { return false; } /* 0x3F400 */
            if (!W(pDat, &pBase->nStoresTotal, 4)) { return false; } /* 0x3F410 */
            if (!W(pDat, &pBase->nUnk8, 4))        { return false; } /* 0x3F424 */
            if (!W(pDat, &pBase->nUnk12, 4))       { return false; } /* 0x3F438 */
            if (!W(pDat, &pBase->bRegionId, 1))       { return false; } /* 0x3F44C */
            if (!W(pDat, &pBase->bUnk16, 1))       { return false; } /* 0x3F460 */

            /* 0x3F470..0x3F4AC: one u16 per object. 0x3F3CC seeds the column offset at
             * 42 (0x2a, auPerBaseVal) and 0x3F4C0 adds 2 per base, so the column is the
             * LOOP INDEX and not BaseRec.bSlot. */
            for (i32 o = 0; o < nObjectCount; ++o) {
                if (apObjects == NULL || apObjects[o] == NULL || b >= 8) {
                    return false;
                }
                if (!W(pDat, &apObjects[o]->auPerBaseVal[b], 2)) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool Save_ReadBases(DatFile *pDat, i32 nSaveFmtVer, SaveBasesState *pState,
                    BaseRec **apBases, i32 nOldBaseCount, u8 bUnk186558,
                    ObjectRecFull *const *apObjects, i32 nObjectCount)
{
    if (pDat == NULL || pState == NULL) {
        return false;
    }

    /* 0x402D0..0x40310: free the records the game still holds BEFORE the chunk is
     * read, using the count from before the read, not the one in the file. */
    if (apBases != NULL) {
        for (i32 i = 0; i < nOldBaseCount; ++i) {
            free(apBases[i]);
            apBases[i] = NULL;
        }
    }

    /* 0x4031C: the original does not test this. A failed lookup shows up as the
     * reads failing, so the port stops here instead. */
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_BASES)) {
        return false;
    }

    if (!DatFile_Read(pDat, pState->abBlock288, 288))   { return false; } /* 0x4032C */
    if (!DatFile_Read(pDat, pState->abBlock1152, 1152)) { return false; } /* 0x4033C */
    if (!DatFile_Read(pDat, &pState->bBaseCount, 1))    { return false; } /* 0x40354 */
    if (!DatFile_Read(pDat, &pState->bUnk159D81, 1))    { return false; } /* 0x4036C */
    if (!DatFile_Read(pDat, &pState->bUnk159D82, 1))    { return false; } /* 0x40384 */
    if (!DatFile_Read(pDat, pState->abUnk159D90, 4))    { return false; } /* 0x4039C */

    /* 0x403A4..0x403C4: 8 bytes on a modern save, 4 on an old one. The writer always
     * emits 8, so the four upper bytes keep whatever they had on a version < 3 read. */
    if (!DatFile_Read(pDat, pState->abUnk159DBC,
                      (nSaveFmtVer >= 3) ? 8u : 4u)) {
        return false;
    }

    if (!DatFile_Read(pDat, pState->abUnk1598D8, 4)) { return false; } /* 0x403D4 */

    /* 0x403E0..0x403F0: this byte is only read on version >= 3. */
    if (nSaveFmtVer >= 3) {
        if (!DatFile_Read(pDat, &pState->bUnk158848, 1)) {
            return false;
        }
    }

    for (i32 i = 0; i < PUFO_BASE_FLAG_COUNT; ++i) {
        if (!DatFile_Read(pDat, &pState->abFlag20[i], 1)) {
            return false;
        }
    }

    if (!DatFile_Read(pDat, pState->aRegionBuildCount, 9)) { return false; } /* 0x40428 */

    for (i32 f = 0; f < PUFO_BASE_FAC_COUNT; ++f) {
        if (!ReadFacility(pDat, &pState->aFac[f])) {
            return false;
        }
    }

    if ((i8)pState->bBaseCount > 0 && apBases != NULL) {
        for (i32 b = 0; b < (i8)pState->bBaseCount; ++b) {
            BaseRec *pNew = (BaseRec *)malloc(sizeof(BaseRec));

            if (pNew == NULL) {
                return false;
            }
            memset(pNew, 0, sizeof(BaseRec));

            /* 0x404F8..0x40534: new(0x14) then the ctor, then store. The ctor is handed
             * the byte at 0x159D80, which here is the base COUNT just read, so it zeroes
             * auPerBaseVal[nBaseCount] of every object -- one past the last column the
             * writer produced. Its three field writes are all overwritten below. */
            (void)BaseRec_Ctor(pNew, (i8)pState->bBaseCount, bUnk186558,
                               apObjects, nObjectCount);
            apBases[b] = pNew;

            if (!DatFile_Read(pDat, &pNew->bSlot, 1))        { return false; } /* 0x40544 */
            if (!DatFile_Read(pDat, &pNew->bUnk4, 1))        { return false; } /* 0x40558 */
            if (!DatFile_Read(pDat, &pNew->nStoresTotal, 4)) { return false; } /* 0x40568 */
            if (!DatFile_Read(pDat, &pNew->nUnk8, 4))        { return false; } /* 0x4057C */
            if (!DatFile_Read(pDat, &pNew->nUnk12, 4))       { return false; } /* 0x40590 */
            if (!DatFile_Read(pDat, &pNew->bRegionId, 1))       { return false; } /* 0x405A4 */
            if (!DatFile_Read(pDat, &pNew->bUnk16, 1))       { return false; } /* 0x405B8 */

            for (i32 o = 0; o < nObjectCount; ++o) {
                if (apObjects == NULL || apObjects[o] == NULL || b >= 8) {
                    return false;
                }
                if (!DatFile_Read(pDat, &apObjects[o]->auPerBaseVal[b], 2)) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* One 64-byte site record (MEMORY[0x186C40], stride 0x40) joined with its 61-byte wire form. The nine
 * offsets are the writer's own stores (0x3F31C abName +0x1A through 0x3F39C nUnkDemolishState +0x34) and
 * the reader mirrors them (0x40440..0x404C0), so the same list runs in both directions. */
static void BasesSiteRecordJoin(u8 *pRec, SaveBaseFacility *pFac, bool bToWire)
{
    if (bToWire) {
        memcpy(pFac->szName, pRec + 0x1A, 20);
        pFac->bType = pRec[0x00];
        memcpy(&pFac->nBuildState, pRec + 0x38, 4);
        memcpy(pFac->szUnk6, pRec + 0x06, 20);
        memcpy(&pFac->nFacType, pRec + 0x3C, 4);
        memcpy(&pFac->nUnk48, pRec + 0x30, 4);
        memcpy(&pFac->uUnk2, pRec + 0x02, 2);
        memcpy(&pFac->uUnk4, pRec + 0x04, 2);
        memcpy(&pFac->nDemolishState, pRec + 0x34, 4);
    } else {
        memcpy(pRec + 0x1A, pFac->szName, 20);
        pRec[0x00] = pFac->bType;
        memcpy(pRec + 0x38, &pFac->nBuildState, 4);
        memcpy(pRec + 0x06, pFac->szUnk6, 20);
        memcpy(pRec + 0x3C, &pFac->nFacType, 4);
        memcpy(pRec + 0x30, &pFac->nUnk48, 4);
        memcpy(pRec + 0x02, &pFac->uUnk2, 2);
        memcpy(pRec + 0x04, &pFac->uUnk4, 2);
        memcpy(pRec + 0x34, &pFac->nDemolishState, 4);
    }
}

/* SaveBasesState is the port's second home for values the original keeps in one place each, so the
 * driver joins the two. The mapping IS the bases chunk's write list (0x3F21C..0x3F3A4), run the other
 * way for the load (0x4032C..0x404C0):
 *
 *   0x159768, 288 B   grid[0..287]
 *   0x1598DC, 1152 B  grid + 372 .. grid + 1523   (0x1598DC - 0x159768 = 372)
 *   0x159D80          nBaseCount, signed byte
 *   0x159D81          nSelectedBaseIdx
 *   0x159DBC, 8 B     the eight hangar bytes
 *   0x1598D8, 4 B     nFunds (the original's sits at grid + 368, inside the gap the two blocks skip)
 *   0x158848          nCraftHomeBase
 *   0x186E48, 17 x 1  aBaseFacFlags[i].bFlag20: record stride 0x1C, the flag at +0x14
 *   0x18655D, 9 B     the region counters
 *   0x186C40, 8 x 61  the eight site records
 *
 * unk_159D82 is not here: the shell has no live cell for it, so the record is its only home and it
 * round trips through the record. nSoldierHireCount @ 0x159D90 now has a live cell (the buy/sell
 * screen's hire tally) and travels with the two joins. */
void Save_BasesLiveToRecord(const SaveBasesLive *pLive, SaveBasesState *pState)
{
    if (pLive == NULL || pState == NULL) {
        return; /* the format test drives the chunk with a record of its own and no live world */
    }

    memcpy(pState->abBlock288, pLive->abFacilityGrid, sizeof(pState->abBlock288));            /* 0x3F21C */
    memcpy(pState->abBlock1152, pLive->abFacilityGrid + 372, sizeof(pState->abBlock1152));    /* 0x3F22C */
    pState->bBaseCount = (u8)*pLive->pnBaseCount;                       /* 0x3F244 */
    pState->bUnk159D81 = (u8)*pLive->pnSelectedBase;                    /* 0x3F25C */
    memcpy(pState->abUnk159DBC, pLive->abHangarBytes, sizeof(pState->abUnk159DBC)); /* 0x3F2A4 */
    memcpy(pState->abUnk1598D8, pLive->pnFunds, sizeof(pState->abUnk1598D8));       /* 0x3F2B4 */
    memcpy(pState->abUnk159D90, pLive->pnSoldierHireCount, 4);                      /* 0x3F28C, the dword's 4 bytes */
    pState->bUnk158848 = (u8)*pLive->pnCraftHomeBase;                   /* 0x3F2C8 */
    for (i32 i = 0; i < PUFO_BASE_FLAG_COUNT; ++i) {
        pState->abFlag20[i] = pLive->abBaseFacRecords[i * 28 + 0x14];   /* 0x3F2E0, v0 += 0x1C */
    }
    memcpy(pState->aRegionBuildCount, pLive->aRegionBuildCount,
           sizeof(pState->aRegionBuildCount));                          /* 0x3F304 */
    for (i32 f = 0; f < PUFO_BASE_FAC_COUNT; ++f) {
        BasesSiteRecordJoin(pLive->abSiteRecords + f * 64, &pState->aFac[f], true); /* 0x3F308..0x3F3A4 */
    }
}

void Save_BasesRecordToLive(const SaveBasesState *pState, const SaveBasesLive *pLive)
{
    if (pLive == NULL || pState == NULL) {
        return;
    }

    memcpy(pLive->abFacilityGrid, pState->abBlock288, sizeof(pState->abBlock288));            /* 0x4032C */
    memcpy(pLive->abFacilityGrid + 372, pState->abBlock1152, sizeof(pState->abBlock1152));    /* 0x4033C */
    *pLive->pnBaseCount = (i8)pState->bBaseCount;                       /* 0x40354 */
    *pLive->pnSelectedBase = (i8)pState->bUnk159D81;                    /* 0x4036C */
    memcpy(pLive->abHangarBytes, pState->abUnk159DBC, sizeof(pState->abUnk159DBC)); /* 0x403C4 */
    memcpy(pLive->pnFunds, pState->abUnk1598D8, sizeof(pState->abUnk1598D8));       /* 0x403D4 */
    memcpy(pLive->pnSoldierHireCount, pState->abUnk159D90, 4);                      /* 0x4039C, the dword's 4 bytes */
    *pLive->pnCraftHomeBase = (i8)pState->bUnk158848;                   /* 0x403F0 */
    for (i32 i = 0; i < PUFO_BASE_FLAG_COUNT; ++i) {
        pLive->abBaseFacRecords[i * 28 + 0x14] = pState->abFlag20[i];   /* 0x40408, v3 += 0x1C */
    }
    memcpy(pLive->aRegionBuildCount, pState->aRegionBuildCount,
           sizeof(pState->aRegionBuildCount));                          /* 0x40428 */
    for (i32 f = 0; f < PUFO_BASE_FAC_COUNT; ++f) {
        BasesSiteRecordJoin(pLive->abSiteRecords + f * 64, (SaveBaseFacility *)&pState->aFac[f], false);
        /* 0x40430..0x404C0. The cast drops the const of the SOURCE field, not of pRec: the record is read
         * here and written into the live record, and BasesSiteRecordJoin only ever reads pFac when
         * bToWire is false. */
    }
}

bool Save_WriteShips(DatFile *pDat, const Ship *const *apShips, i32 nShipCount,
                     ObjectRecFull *const *apObjects, i32 nObjectCount)
{
    if (pDat == NULL) {
        return false;
    }
    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_SHIPS)) {
        return false; /* 0x3F6E8 */
    }

    /* 0x3F704: the count is saved as ONE byte of a 4-byte global. */
    {
        u8 bCount = (u8)nShipCount;
        if (!W(pDat, &bCount, 1)) {
            return false;
        }
    }

    for (i32 s = 0; s < nShipCount; ++s) {
        const Ship *pShip = (apShips != NULL) ? apShips[s] : NULL;

        if (pShip == NULL) {
            return false;
        }
        if (!W(pDat, &pShip->nFuel, 2))         { return false; } /* 0x3F740 */
        if (!W(pDat, &pShip->bHomeBase, 1))     { return false; } /* 0x3F754 */
        if (!W(pDat, &pShip->nSlotIdx, 4))      { return false; } /* 0x3F768 */
        if (!W(pDat, &pShip->bCraftType, 1))    { return false; } /* 0x3F77C */
        if (!W(pDat, &pShip->bWeaponL, 1))      { return false; } /* 0x3F790 */
        if (!W(pDat, &pShip->uRangeRemain, 2))  { return false; } /* 0x3F7A4 */
        if (!W(pDat, &pShip->bHangarX, 1))      { return false; } /* 0x3F7B8 */
        if (!W(pDat, &pShip->bHangarY, 1))      { return false; } /* 0x3F7CC */
        if (!W(pDat, pShip->anCargoQty, 16))    { return false; } /* 0x3F7E0 */
        if (!W(pDat, &pShip->bNeedsSupply, 1))  { return false; } /* 0x3F7F4 */
        if (!W(pDat, &pShip->bCanDogfight, 1))  { return false; } /* 0x3F808 */
        if (!W(pDat, &pShip->uUnk4, 2))         { return false; } /* 0x3F81C */
        if (!W(pDat, &pShip->uFuelMax, 2))      { return false; } /* 0x3F830 */
        if (!W(pDat, &pShip->uRangeMax, 2))     { return false; } /* 0x3F844 */
        if (!W(pDat, &pShip->bCrewSlots, 1))    { return false; } /* 0x3F858 */
        if (!W(pDat, &pShip->bType, 1))         { return false; } /* 0x3F868: pBuf = the record start */
        if (!W(pDat, &pShip->bAtBase, 1))       { return false; } /* 0x3F87C */
        if (!W(pDat, &pShip->bWeaponsHot, 1))   { return false; } /* 0x3F890 */
        if (!W(pDat, &pShip->bAmmo27, 1))       { return false; } /* 0x3F8A4 */
        if (!W(pDat, &pShip->bAmmo30, 1))       { return false; } /* 0x3F8B8 */
        if (!W(pDat, &pShip->bAmmo29, 1))       { return false; } /* 0x3F8CC */
        if (!W(pDat, &pShip->bAmmo25, 1))       { return false; } /* 0x3F8E0 */
        if (!W(pDat, &pShip->bAmmo26, 1))       { return false; } /* 0x3F8F4 */
        if (!W(pDat, &pShip->bRefueling, 1))    { return false; } /* 0x3F908 */
        if (!W(pDat, &pShip->bWeaponR, 1))      { return false; } /* 0x3F91C */
        if (!W(pDat, &pShip->bId, 1))           { return false; } /* 0x3F930 */
        if (!W(pDat, &pShip->uFuelCap, 2))      { return false; } /* 0x3F944 */
        if (!W(pDat, &pShip->uUnk14, 2))        { return false; } /* 0x3F958 */
        if (!W(pDat, &pShip->uRangeRate, 2))    { return false; } /* 0x3F96C */
        if (!W(pDat, &pShip->bIntercepting, 1)) { return false; } /* 0x3F980 */
        if (!W(pDat, &pShip->nStrategIdx, 4))   { return false; } /* 0x3F994 */
        if (!W(pDat, &pShip->bContactMsg, 1))   { return false; } /* 0x3F9A8 */

        /* 0x3F720 seeds the column at 58 (0x3a, aItemQty) and 0x3FA08 adds 2 per ship. */
        for (i32 o = 0; o < nObjectCount; ++o) {
            if (apObjects == NULL || apObjects[o] == NULL || s >= 52) {
                return false;
            }
            if (!W(pDat, &apObjects[o]->aItemQty[s], 2)) {
                return false;
            }
        }
    }
    return true;
}

bool Save_ReadShips(DatFile *pDat, Ship **apShips, i32 *pnShipCount, ShipCtorEnv *pEnv)
{
    i32 nOldCount;
    u8  bCount;

    if (pDat == NULL || pnShipCount == NULL || pEnv == NULL) {
        return false;
    }

    /* 0x4087C: FindChunk first, THEN free -- the opposite order from Save_ReadBases. */
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_SHIPS)) {
        return false;
    }

    nOldCount = *pnShipCount;
    if (apShips != NULL) {
        for (i32 i = 0; i < nOldCount; ++i) {
            free(apShips[i]);
            apShips[i] = NULL;
        }
    }

    if (!DatFile_Read(pDat, &bCount, 1)) { /* 0x408E0 */
        return false;
    }
    *pnShipCount = (i32)bCount;

    for (i32 s = 0; s < (i32)bCount; ++s) {
        Ship *pNew = (Ship *)malloc(sizeof(Ship));

        if (pNew == NULL) {
            return false;
        }
        memset(pNew, 0, sizeof(Ship));

        /* 0x40908..0x40944: new(0x44) then Ship_Ctor(type 0, id = loop index, home 0,
         * skip 0), then store. The ctor's nSlotIdx is the count just read, so it zeroes
         * aItemQty[nShipCount] of every object, and its type-0 stats are all
         * overwritten by the reads below. */
        (void)Ship_Ctor(pNew, 0, s, 0, 0, pEnv);
        apShips[s] = pNew;

        if (!DatFile_Read(pDat, &pNew->nFuel, 2))         { return false; } /* 0x40954 */
        if (!DatFile_Read(pDat, &pNew->bHomeBase, 1))     { return false; } /* 0x40968 */
        if (!DatFile_Read(pDat, &pNew->nSlotIdx, 4))      { return false; } /* 0x4097C */
        if (!DatFile_Read(pDat, &pNew->bCraftType, 1))    { return false; } /* 0x40990 */
        if (!DatFile_Read(pDat, &pNew->bWeaponL, 1))      { return false; } /* 0x409A4 */
        if (!DatFile_Read(pDat, &pNew->uRangeRemain, 2))  { return false; } /* 0x409B8 */
        if (!DatFile_Read(pDat, &pNew->bHangarX, 1))      { return false; } /* 0x409CC */
        if (!DatFile_Read(pDat, &pNew->bHangarY, 1))      { return false; } /* 0x409E0 */
        if (!DatFile_Read(pDat, pNew->anCargoQty, 16))    { return false; } /* 0x409F4 */
        if (!DatFile_Read(pDat, &pNew->bNeedsSupply, 1))  { return false; } /* 0x40A08 */
        if (!DatFile_Read(pDat, &pNew->bCanDogfight, 1))  { return false; } /* 0x40A1C */
        if (!DatFile_Read(pDat, &pNew->uUnk4, 2))         { return false; } /* 0x40A30 */
        if (!DatFile_Read(pDat, &pNew->uFuelMax, 2))      { return false; } /* 0x40A44 */
        if (!DatFile_Read(pDat, &pNew->uRangeMax, 2))     { return false; } /* 0x40A58 */
        if (!DatFile_Read(pDat, &pNew->bCrewSlots, 1))    { return false; } /* 0x40A6C */
        if (!DatFile_Read(pDat, &pNew->bType, 1))         { return false; } /* 0x40A7C */
        if (!DatFile_Read(pDat, &pNew->bAtBase, 1))       { return false; } /* 0x40A90 */
        if (!DatFile_Read(pDat, &pNew->bWeaponsHot, 1))   { return false; } /* 0x40AA4 */
        if (!DatFile_Read(pDat, &pNew->bAmmo27, 1))       { return false; } /* 0x40AB8 */
        if (!DatFile_Read(pDat, &pNew->bAmmo30, 1))       { return false; } /* 0x40ACC */
        if (!DatFile_Read(pDat, &pNew->bAmmo29, 1))       { return false; } /* 0x40AE0 */
        if (!DatFile_Read(pDat, &pNew->bAmmo25, 1))       { return false; } /* 0x40AF4 */
        if (!DatFile_Read(pDat, &pNew->bAmmo26, 1))       { return false; } /* 0x40B08 */
        if (!DatFile_Read(pDat, &pNew->bRefueling, 1))    { return false; } /* 0x40B1C */
        if (!DatFile_Read(pDat, &pNew->bWeaponR, 1))      { return false; } /* 0x40B30 */
        if (!DatFile_Read(pDat, &pNew->bId, 1))           { return false; } /* 0x40B44 */
        if (!DatFile_Read(pDat, &pNew->uFuelCap, 2))      { return false; } /* 0x40B58 */
        if (!DatFile_Read(pDat, &pNew->uUnk14, 2))        { return false; } /* 0x40B6C */
        if (!DatFile_Read(pDat, &pNew->uRangeRate, 2))    { return false; } /* 0x40B80 */
        if (!DatFile_Read(pDat, &pNew->bIntercepting, 1)) { return false; } /* 0x40B94 */
        if (!DatFile_Read(pDat, &pNew->nStrategIdx, 4))   { return false; } /* 0x40BA8 */
        if (!DatFile_Read(pDat, &pNew->bContactMsg, 1))   { return false; } /* 0x40BBC */

        for (i32 o = 0; o < pEnv->nObjectCount; ++o) {
            if (pEnv->apObjects == NULL || pEnv->apObjects[o] == NULL || s >= 52) {
                return false;
            }
            if (!DatFile_Read(pDat, &pEnv->apObjects[o]->aItemQty[s], 2)) {
                return false;
            }
        }

        /* 0x40C24..0x40C2C: after the load, a craft type of 0 forces uRangeMax to 10000. */
        if (pNew->bCraftType == 0) {
            pNew->uRangeMax = 10000;
        }
    }
    return true;
}

bool Save_AllocFlobjs(DatFile *pDat, i32 nWideCountFlag, i32 *pnTactItemCount,
                      TactItem **apTactItems)
{
    if (pDat == NULL || pnTactItemCount == NULL) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_FLOBJS)) { /* 0x431BC */
        return false;
    }

    if (nWideCountFlag != 0) { /* 0x431E0: 4 when Map+0x118 is non-zero, else 1 */
        if (!DatFile_Read(pDat, pnTactItemCount, 4)) {
            return false;
        }
    } else {
        u8 bLow = 0;

        if (!DatFile_Read(pDat, &bLow, 1)) {
            return false;
        }
        /* One byte into a 4-byte little-endian global: the upper three bytes survive. */
        *pnTactItemCount = (*pnTactItemCount & ~0xFF) | bLow;
    }

    for (i32 i = 0; i < *pnTactItemCount; ++i) {
        TactItem *pItem;

        if (apTactItems == NULL) {
            return false;
        }
        pItem = (TactItem *)malloc(sizeof(TactItem));
        if (pItem == NULL) {
            return false;
        }
        /* 0x43210..0x43234: new(0x18) then the ctor, then store. The ctor clears only
         * pNext, bUnk8, bUnk7 and pLinkedSoldier, so the rest is uninitialised here and
         * the flobjs reader fills it in afterwards. */
        memset((void *)pItem + offsetof(TactItem, pObj), 0,
               sizeof(TactItem) - offsetof(TactItem, pObj));
        TactItem_ctor(pItem);
        apTactItems[i] = pItem;
    }
    return true;
}

bool Save_WriteFlobjs(DatFile *pDat, const TactItem *const *apTactItems, i32 nTactItemCount,
                      const ObjectRecFull *const *apObjects, i32 nObjectCount)
{
    if (pDat == NULL) {
        return false;
    }
    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_FLOBJS)) { /* 0x42F58 */
        return false;
    }
    if (!W(pDat, &nTactItemCount, 4)) { /* 0x42F74: the writer is always 4 bytes wide */
        return false;
    }

    for (i32 i = 0; i < nTactItemCount; ++i) {
        const TactItem *pItem = (apTactItems != NULL) ? apTactItems[i] : NULL;
        i32 nObjIndex = 0;
        i32 nLink;
        i32 nSaveId;

        if (pItem == NULL) {
            return false;
        }

        /* 0x42FB0..0x42FE4: a linear search for pObj. A miss leaves the index equal to
         * nObjectCount, and the original writes that out-of-range index and lets the
         * reader dereference it. The port refuses instead. */
        for (i32 o = 0; o < nObjectCount; ++o) {
            if (apObjects != NULL && apObjects[o] == pItem->pObj) {
                break;
            }
            nObjIndex = o + 1;
        }
        if (nObjIndex >= nObjectCount) {
            return false;
        }

        if (!W(pDat, &nObjIndex, 4))        { return false; } /* 0x42FF4 */
        if (!W(pDat, &pItem->nIndex, 2))    { return false; } /* 0x4300C */
        if (!W(pDat, &pItem->nAmmo, 1))     { return false; } /* 0x43020 */
        if (!W(pDat, &pItem->bUnk7, 1))     { return false; } /* 0x43034 */
        if (!W(pDat, &pItem->bUnk8, 1))     { return false; } /* 0x43048 */

        /* 0x43058..0x43070: the NEXT item's pool index, which is what nRefId holds after
         * Save_AssignRosterIndices @ 0x42ea0 has numbered the pool. */
        nLink = (pItem->pNext != NULL) ? pItem->pNext->nRefId : 0x7FFF;
        if (!W(pDat, &nLink, 4)) {
            return false;
        }
        nSaveId = (pItem->pLinkedSoldier != NULL) ? pItem->pLinkedSoldier->nSaveId : 0x7FFF;
        if (!W(pDat, &nSaveId, 4)) { /* 0x43080..0x43098 */
            return false;
        }
    }
    return true;
}

bool Save_ReadFlobjs(DatFile *pDat, i32 nWideCountFlag, i32 *pnTactItemCount,
                     TactItem *const *apTactItems,
                     ObjectRecFull *const *apObjects, i32 nObjectCount,
                     Soldier *const *apSoldiers)
{
    if (pDat == NULL || pnTactItemCount == NULL) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_FLOBJS)) { /* 0x4326C */
        return false;
    }

    /* 0x43274: the same 1-or-4 width gate as Save_AllocFlobjs. A load reads the count
     * twice, once to allocate and once to fill. */
    if (nWideCountFlag != 0) {
        if (!DatFile_Read(pDat, pnTactItemCount, 4)) {
            return false;
        }
    } else {
        u8 bLow = 0;

        if (!DatFile_Read(pDat, &bLow, 1)) {
            return false;
        }
        *pnTactItemCount = (*pnTactItemCount & ~0xFF) | bLow;
    }

    for (i32 i = 0; i < *pnTactItemCount; ++i) {
        TactItem *pItem = (apTactItems != NULL) ? apTactItems[i] : NULL;
        i32 nObjIndex;
        i32 nLink;

        if (pItem == NULL) {
            return false;
        }
        if (!DatFile_Read(pDat, &nObjIndex, 4)) { /* 0x432D4 */
            return false;
        }
        if (nObjIndex < 0 || nObjIndex >= nObjectCount || apObjects == NULL ||
            apObjects[nObjIndex] == NULL) {
            return false;
        }
        pItem->pObj = apObjects[nObjIndex]; /* 0x432F0 */

        if (!DatFile_Read(pDat, &pItem->nIndex, 2))  { return false; } /* 0x43300 */
        if (!DatFile_Read(pDat, &pItem->nAmmo, 1))   { return false; } /* 0x43314 */
        if (!DatFile_Read(pDat, &pItem->bUnk7, 1))   { return false; } /* 0x43328 */
        if (!DatFile_Read(pDat, &pItem->bUnk8, 1))   { return false; } /* 0x4333C */

        if (!DatFile_Read(pDat, &nLink, 4)) { /* 0x4334C */
            return false;
        }
        if (nLink == 0x7FFF) {
            pItem->pNext = NULL;
        } else if (nLink >= 0 && nLink < *pnTactItemCount && apTactItems[nLink] != NULL) {
            pItem->pNext = apTactItems[nLink];
        } else {
            return false;
        }

        if (!DatFile_Read(pDat, &nLink, 4)) { /* 0x4337C */
            return false;
        }
        if (nLink == 0x7FFF) {
            pItem->pLinkedSoldier = NULL;
        } else if (nLink >= 0 && apSoldiers != NULL) {
            /* The original indexes apSoldiers with no bound check; the port cannot know
             * the roster size here, so it only rejects a negative index. */
            pItem->pLinkedSoldier = apSoldiers[nLink];
        } else {
            return false;
        }
    }
    return true;
}

/* One bit as its own byte, which is how the map chunk stores every flag. */
static bool WriteBit(DatFile *pDat, u32 nBit)
{
    u8 bBit = (u8)(nBit & 1u);

    return W(pDat, &bBit, 1);
}

static bool ReadBit(DatFile *pDat, u32 *pnBit)
{
    u8 bBit = 0;

    if (!DatFile_Read(pDat, &bBit, 1)) {
        return false;
    }
    *pnBit = bBit & 1u;
    return true;
}

/* One byte into a word. The reader keeps the whole byte for the two nibble fields
 * of nFlags32 and nFlags40, so it must not be narrowed to a bit. */
static bool ReadByte(DatFile *pDat, u32 *pnVal)
{
    u8 bVal = 0;

    if (!DatFile_Read(pDat, &bVal, 1)) {
        return false;
    }
    *pnVal = bVal;
    return true;
}

bool Save_WriteMap(DatFile *pDat, const MapGeom *pMap,
                   MapCellLookup pfnLookup, void *pUser)
{
    if (pDat == NULL || pMap == NULL || pfnLookup == NULL) {
        return false;
    }
    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_MAP)) { /* 0x438F0 */
        return false;
    }
    if (!W(pDat, &pMap->nDimX, 1)) { /* Map.nMapDimX @ +86299 */
        return false;
    }
    if (!W(pDat, &pMap->nDimY, 1)) { /* Map.nMapDimY @ +86300 */
        return false;
    }

    for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (i32 nRow = 0; nRow < PUFO_CELL_SIZE * (i32)pMap->nDimY; ++nRow) {
            for (i32 nCol = 0; nCol < PUFO_CELL_SIZE * (i32)pMap->nDimX; ++nCol) {
                MapCell *pCell = pfnLookup(nCol, nRow, nLayer, pUser);
                u32 nLow, nHigh;
                i32 nIndex;

                if (pCell == NULL) {
                    /* Seam guard (Part 103): the chunk is positional -- one record per walk slot,
                     * record order == slot order, and the record's own cell coordinates are derived
                     * from the walk (0x434DC..0x434E4 on the read side). The original tests nothing in
                     * either walk (0x4398C/0x43994 write, 0x434B4/0x434BC read dereference [slot]+0x10
                     * straight) because its inline table 0x3D138..0x3D1CC is complete over the walk
                     * range. A NULL inside the range in the shell's grid means the grid and the walk
                     * are no longer in step; skipping would desync the positional binding -- exactly
                     * the Part 65 grid-index shape (59 slots in 60 NULL, records landing on the next
                     * slot) -- so the writer refuses instead of emitting a short stream. Deviation
                     * recorded: the original dereferences NULL. */
                    return false;
                }

                if (!W(pDat, pCell->abFaceDamage, 4)) { /* 0x43998: the four face counters as a block */
                    return false;
                }
                for (u32 nBit = 0; nBit < 5; ++nBit) { /* 0x439B0..0x43A34 */
                    if (!WriteBit(pDat, (u32)pCell->bFlags29 >> nBit)) {
                        return false;
                    }
                }
                nLow  = (u32)pCell->nFlags32 & 7u;           /* 0x43A4C */
                nHigh = ((u32)pCell->nFlags32 >> 3) & 0xFu;  /* 0x43A6C */
                if (!W(pDat, &nLow, 1) || !W(pDat, &nHigh, 1)) {
                    return false;
                }
                if (!WriteBit(pDat, (u32)pCell->bFlags36)) { /* 0x43A88 */
                    return false;
                }
                nLow  = (u32)pCell->nFlags40 & 7u;           /* 0x43AA4 */
                nHigh = ((u32)pCell->nFlags40 >> 3) & 0xFu;  /* 0x43AC0 */
                if (!W(pDat, &nLow, 1) || !W(pDat, &nHigh, 1)) {
                    return false;
                }
                if (!WriteBit(pDat, (u32)pCell->bFlags44)) { /* 0x43AE0 */
                    return false;
                }

                nIndex = (pCell->pUnit != NULL) ? pCell->pUnit->nSaveId : 0x7FFF;
                if (!W(pDat, &nIndex, 4)) { /* 0x43AF8..0x43B08 */
                    return false;
                }
                nIndex = (pCell->pItemList != NULL) ? pCell->pItemList->nRefId : 0x7FFF;
                if (!W(pDat, &nIndex, 4)) { /* 0x43B10..0x43B2C */
                    return false;
                }

                if (!W(pDat, &pCell->bUnk56, 1)) { /* 0x43B3C */
                    return false;
                }
                if (!WriteBit(pDat, (u32)pCell->bFlags57[0] >> 2)) { /* 0x43B58 */
                    return false;
                }
                if (!WriteBit(pDat, (u32)pCell->bFlags57[0] >> 3)) { /* 0x43B78 */
                    return false;
                }
                if (!W(pDat, &pCell->bFlags57[1], 1)) { /* 0x43B8C, IDA's bFlags58 */
                    return false;
                }

                { /* 0x43BA0..0x43C1C: the four sprites as their nGlobalIdx */
                    const Mc1Rec *const apSpr[4] = {
                        pCell->pFloorSpr, pCell->pWallSprA, pCell->pWallSprB, pCell->pCeilSpr
                    };

                    for (u32 nWhich = 0; nWhich < 4; ++nWhich) {
                        nIndex = (apSpr[nWhich] != NULL)
                               ? (i32)apSpr[nWhich]->nGlobalIdx
                               : 0x7FFF;
                        if (!W(pDat, &nIndex, 4)) {
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

/* The map chunk's two dimension bytes (0x43400 / 0x43418), read ahead of the shell's grid
 * allocation. The original needs no such step: its 14400-entry cell table is INLINE in Map
 * (Map+0x15338..Map+0x23438) and survives the mission, so Save_ReadMap's walk over
 * 4 layers x dimY*10 x dimX*10 always meets the cells its writer wrote. The shell mallocs the grid
 * from the dims, so it has to know them first -- allocating from the previous (or zero) dims
 * leaves fewer cells than the chunk holds records, and the two walks desynchronise.
 * Save_ReadMap re-finds the chunk (its own 0x433E8), so the pre-read leaves no cursor debt. */
bool Save_ReadMapDims(DatFile *pDat, MapGeom *pMap)
{
    if (pDat == NULL || pMap == NULL) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_MAP)) {
        return false;
    }
    if (!DatFile_Read(pDat, &pMap->nDimX, 1)) { /* 0x43400 */
        return false;
    }
    return DatFile_Read(pDat, &pMap->nDimY, 1); /* 0x43418 */
}

bool Save_ReadMap(DatFile *pDat, MapGeom *pMap, MapCellLookup pfnLookup, void *pUser,
                  Soldier *const *apSoldiers, i32 nSoldierCount,
                  TactItem *const *apItems, i32 nItemCount,
                  const PufoFont *pFont)
{
    if (pDat == NULL || pMap == NULL || pfnLookup == NULL) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_MAP)) { /* 0x433E8 */
        return false;
    }
    if (!DatFile_Read(pDat, &pMap->nDimX, 1)) { /* 0x43400 */
        return false;
    }
    if (!DatFile_Read(pDat, &pMap->nDimY, 1)) { /* 0x43418 */
        return false;
    }

    for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
        for (i32 nRow = 0; nRow < PUFO_CELL_SIZE * (i32)pMap->nDimY; ++nRow) {
            for (i32 nCol = 0; nCol < PUFO_CELL_SIZE * (i32)pMap->nDimX; ++nCol) {
                MapCell *pCell = pfnLookup(nCol, nRow, nLayer, pUser);
                u32 aBit[5];
                u32 nLow, nHigh;
                i32 nIndex;

                if (pCell == NULL) {
                    /* Seam guard (Part 103), the read-side half of the Save_WriteMap one: a NULL slot
                     * inside the walk range means the grid and the record stream are out of step;
                     * consuming the next slot's record here is the Part 65 desync. Refuse instead.
                     * Deviation recorded: the original dereferences NULL (0x434B4/0x434BC). */
                    return false;
                }

                if (!DatFile_Read(pDat, pCell->abFaceDamage, 4)) { /* 0x434C0 */
                    return false;
                }
                pCell->nPathCost = 0;  /* 0x434D0 */
                pCell->bPathDir = 0;
                pCell->bPathFlag = 0;
                pCell->nX = (u8)nCol; /* 0x434DC: coordinates are derived, not stored */
                pCell->nY = (u8)nRow;
                pCell->nZ = (u8)nLayer;

                for (u32 nBit = 0; nBit < 5; ++nBit) { /* 0x434E8..0x435B0 */
                    if (!ReadBit(pDat, &aBit[nBit])) {
                        return false;
                    }
                }
                /* The five stores are individually `& mask | bit`, and the last mask is
                 * 0x8F, which also clears bits 5 and 6. Bit 5 is the shot-trace flag, so a
                 * load wipes it for every cell while restoring bit 4. Bit 7 survives. */
                pCell->bFlags29 = (u8)(((u32)pCell->bFlags29 & 0x8Fu) |
                                       (aBit[0] | (aBit[1] << 1) | (aBit[2] << 2) |
                                       (aBit[3] << 3) | (aBit[4] << 4)));

                if (!ReadByte(pDat, &nLow) || !ReadByte(pDat, &nHigh)) { /* 0x435BC, 0x435E8 */
                    return false;
                }
                nLow &= 7u;
                nHigh &= 0xFu;
                if (!ReadBit(pDat, &aBit[0])) { /* 0x43610 */
                    return false;
                }
                pCell->nFlags32 = ((u32)pCell->nFlags32 & 0xFFFFFF87u) | nLow | (nHigh << 3);
                pCell->bFlags36 = (u8)(((u32)pCell->bFlags36 & ~1u) | aBit[0]);

                if (!ReadByte(pDat, &nLow) || !ReadByte(pDat, &nHigh)) { /* 0x43644, 0x43670 */
                    return false;
                }
                nLow &= 7u;
                nHigh &= 0xFu;
                if (!ReadBit(pDat, &aBit[0])) { /* 0x43698 */
                    return false;
                }
                pCell->nFlags40 = ((u32)pCell->nFlags40 & 0xFFFFFF87u) | nLow | (nHigh << 3);
                /* 0x436BC is `bFlags44 & 0xFC ^ value`, which clears bit 1 on the way in. */
                pCell->bFlags44 = (u8)(((u32)pCell->bFlags44 & 0xFCu) | aBit[0]);

                if (!DatFile_Read(pDat, &nIndex, 4)) { /* 0x436C0 */
                    return false;
                }
                if (nIndex == 0x7FFF) {
                    pCell->pUnit = NULL;
                } else if (nIndex >= 0 && nIndex < nSoldierCount && apSoldiers != NULL) {
                    pCell->pUnit = apSoldiers[nIndex];
                } else {
                    return false;
                }

                if (!DatFile_Read(pDat, &nIndex, 4)) { /* 0x436F0 */
                    return false;
                }
                if (nIndex == 0x7FFF) {
                    pCell->pItemList = NULL;
                } else if (nIndex >= 0 && nIndex < nItemCount && apItems != NULL) {
                    pCell->pItemList = apItems[nIndex];
                } else {
                    return false;
                }

                if (!DatFile_Read(pDat, &pCell->bUnk56, 1)) { /* 0x43720 */
                    return false;
                }
                if (!ReadBit(pDat, &aBit[0]) || !ReadBit(pDat, &aBit[1])) { /* 0x43730, 0x43758 */
                    return false;
                }
                pCell->bFlags57[0] = (u8)(((u32)pCell->bFlags57[0] & 0xF3u) |
                                          ((aBit[0] & 1u) << 2) | ((aBit[1] & 1u) << 3));
                if (!DatFile_Read(pDat, &pCell->bFlags57[1], 1)) { /* 0x43780 */
                    return false;
                }

                { /* 0x43790..0x4385C: Font_GetSprByIndex on the stored nGlobalIdx */
                    Mc1Rec **pppSpr[4] = {
                        &pCell->pFloorSpr, &pCell->pWallSprA, &pCell->pWallSprB, &pCell->pCeilSpr
                    };

                    for (u32 nWhich = 0; nWhich < 4; ++nWhich) {
                        if (!DatFile_Read(pDat, &nIndex, 4)) {
                            return false;
                        }
                        if (nIndex == 0x7FFF) {
                            *pppSpr[nWhich] = NULL;
                        } else if (nIndex >= 0 && pFont != NULL) {
                            *pppSpr[nWhich] = Font_GetSprByIndex(pFont, (u16)nIndex);
                            if (*pppSpr[nWhich] == NULL) {
                                return false;
                            }
                        } else {
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

/* One roster slot list: nSaveId, or 0x7FFF for an empty slot. The writer only
 * looks at slots below the count, so a pointer past the count is not saved. */
static bool WriteRosterIds(DatFile *pDat, Soldier *const *apUnits, i32 nCount, i32 nSlots)
{
    for (i32 i = 0; i < nSlots; ++i) {
        i32 nId = (i < nCount && apUnits != NULL && apUnits[i] != NULL)
                ? apUnits[i]->nSaveId
                : 0x7FFF;

        if (!W(pDat, &nId, 4)) {
            return false;
        }
    }
    return true;
}

/* The reader always consumes the full slot list and ignores the count it just read. */
static bool ReadRosterIds(DatFile *pDat, Soldier **apDst, i32 nSlots,
                          Soldier *const *apSoldiers, i32 nSoldierCount)
{
    for (i32 i = 0; i < nSlots; ++i) {
        i32 nId;

        if (!DatFile_Read(pDat, &nId, 4)) {
            return false;
        }
        if (nId == 0x7FFF) {
            apDst[i] = NULL;
            continue;
        }
        if (nId < 0 || nId >= nSoldierCount || apSoldiers == NULL) {
            return false;
        }
        apDst[i] = apSoldiers[nId];
    }
    return true;
}

bool Save_WriteTeamUnits(DatFile *pDat, const TeamUnitsState *pState)
{
    if (pDat == NULL || pState == NULL) {
        return false;
    }

    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_TEAMUNITS)) { /* 0x43C94 */
        return false;
    }
    if (!W(pDat, &pState->nXcomCount, 1)) { /* 0x43CAC: one byte, not four */
        return false;
    }
    {
        i32 nId = (pState->pSelUnit != NULL) ? pState->pSelUnit->nSaveId : 0x7FFF;

        if (!W(pDat, &nId, 4)) { /* 0x43CC8..0x43CE0: the selection, before the roster */
            return false;
        }
    }
    if (!WriteRosterIds(pDat, pState->apXcomUnits, pState->nXcomCount,
                        PUFO_SAVE_ROSTER_MAX)) { /* 0x43CE4..0x43D38 */
        return false;
    }

    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_ALIENS)) { /* 0x43D44 */
        return false;
    }
    if (!W(pDat, &pState->nEnemyCount, 1)) {
        return false;
    }
    if (!WriteRosterIds(pDat, pState->apEnemyUnits, pState->nEnemyCount,
                        PUFO_SAVE_ROSTER_MAX)) { /* 0x43D60..0x43DB8 */
        return false;
    }

    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_DIEDSOLDS)) { /* 0x43DC4 */
        return false;
    }
    if (!W(pDat, &pState->nCivilianCount, 4)) { /* 0x43DDC: four bytes here */
        return false;
    }
    if (!WriteRosterIds(pDat, pState->apCivilians, pState->nCivilianCount,
                        PUFO_SAVE_CIV_MAX)) { /* 0x43DE0..0x43E38 */
        return false;
    }

    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_HIDDENSOL)) { /* 0x43E44 */
        return false;
    }
    if (!W(pDat, &pState->nHiddenCount, 4)) {
        return false;
    }
    return WriteRosterIds(pDat, pState->apHiddenUnits, pState->nHiddenCount,
                          PUFO_SAVE_ROSTER_MAX); /* 0x43E60..0x43EB8 */
}

bool Save_ReadTeamUnits(DatFile *pDat, TeamUnitsState *pState,
                        Soldier *const *apSoldiers, i32 nSoldierCount)
{
    if (pDat == NULL || pState == NULL) {
        return false;
    }

    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_TEAMUNITS)) { /* 0x44030 */
        return false;
    }
    if (!DatFile_Read(pDat, &pState->nXcomCount, 1)) {
        return false;
    }
    {
        i32 nId;

        if (!DatFile_Read(pDat, &nId, 4)) { /* 0x44058 */
            return false;
        }
        if (nId == 0x7FFF) {
            pState->pSelUnit = NULL;
        } else if (nId >= 0 && nId < nSoldierCount && apSoldiers != NULL) {
            pState->pSelUnit = apSoldiers[nId];
        } else {
            return false;
        }
    }
    if (!ReadRosterIds(pDat, pState->apXcomUnits, PUFO_SAVE_ROSTER_MAX,
                       apSoldiers, nSoldierCount)) {
        return false;
    }

    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_ALIENS)) { /* 0x440E8 */
        return false;
    }
    if (!DatFile_Read(pDat, &pState->nEnemyCount, 1)) {
        return false;
    }
    if (!ReadRosterIds(pDat, pState->apEnemyUnits, PUFO_SAVE_ROSTER_MAX,
                       apSoldiers, nSoldierCount)) {
        return false;
    }

    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_DIEDSOLDS)) { /* 0x44154 */
        return false;
    }
    if (!DatFile_Read(pDat, &pState->nCivilianCount, 4)) {
        return false;
    }
    if (!ReadRosterIds(pDat, pState->apCivilians, PUFO_SAVE_CIV_MAX,
                       apSoldiers, nSoldierCount)) {
        return false;
    }

    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_HIDDENSOL)) { /* 0x441C0 */
        return false;
    }
    if (!DatFile_Read(pDat, &pState->nHiddenCount, 4)) {
        return false;
    }
    return ReadRosterIds(pDat, pState->apHiddenUnits, PUFO_SAVE_ROSTER_MAX,
                         apSoldiers, nSoldierCount);
}

bool Save_WriteTGameData(DatFile *pDat, const TactSaveState *pSt,
                         const TeamUnitsState *pRosters, const FlobjsState *pFlobjs,
                         const MapGeom *pMap, MapCellLookup pfnLookup, void *pUser)
{
    if (pDat == NULL || pSt == NULL || pRosters == NULL || pFlobjs == NULL || pMap == NULL) {
        return false;
    }

    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_TGAMEDATA)) { /* 0x446B4 */
        return false;
    }
    if (!W(pDat, &pSt->bUnk15110, 1)) { /* 0x446D0 */
        return false;
    }
    if (!W(pDat, &pSt->nUnk1510C, 4)) { /* 0x446E8 */
        return false;
    }
    for (u32 i = 0; i < 3; ++i) { /* 0x446FC, 0x44710, 0x44724 */
        if (!W(pDat, &pSt->abUnk15111[i], 1)) {
            return false;
        }
    }
    if (!W(pDat, &pSt->nMapLayers, 1)) { /* 0x4473C: gap_86272[20], the layer count */
        return false;
    }
    if (!W(pDat, &pSt->bUnk15115, 1)) { /* 0x44750 */
        return false;
    }
    /* 0x44764: one dword written as four bytes, and the reader uses each byte as a
     * sprite-set id, 23 meaning "no set for this slot". */
    if (!W(pDat, pSt->abSpriteSet, 4)) {
        return false;
    }
    if (!W(pDat, &pSt->nMissionType, 1)) { /* 0x44778 */
        return false;
    }
    if (!W(pDat, &pSt->bMissionMode, 1)) { /* 0x4478C: unk_128AD5, Map+0x1511D */
        return false;
    }
    if (!W(pDat, &pSt->bUnk1511E, 1)) { /* 0x447A0: unk_128AD6, Map+0x1511E */
        return false;
    }
    for (u32 i = 0; i < 4; ++i) { /* 0x447B8, 0x447D0, 0x447E8, 0x44800 */
        if (!W(pDat, &pSt->anUnk15188[i], 4)) {
            return false;
        }
    }
    for (u32 i = 0; i < 3; ++i) { /* 0x44818, 0x4482C, 0x44840 */
        if (!W(pDat, &pSt->abUnk15198[i], 1)) {
            return false;
        }
    }
    if (!W(pDat, &pSt->nUnk151A0, 4)) { /* 0x44858 */
        return false;
    }
    for (u32 i = 0; i < 3; ++i) { /* 0x44870, 0x44888, 0x448A0: dwords written as bytes */
        if (!W(pDat, &pSt->anCraftTile[i], 1)) {
            return false;
        }
    }

    if (!Save_WriteTeamUnits(pDat, pRosters)) { /* 0x448A4 */
        return false;
    }
    if (!Save_WriteFlobjs(pDat, pFlobjs->apTactItems, pFlobjs->nTactItemCount,
                          pFlobjs->apObjects, pFlobjs->nObjectCount)) { /* 0x448A8 */
        return false;
    }
    if (!Save_WriteMap(pDat, pMap, pfnLookup, pUser)) { /* 0x448AC */
        return false;
    }

    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_V124)) { /* 0x448B8 */
        return false;
    }
    if (!W(pDat, &pSt->nUnk138E6C, 4)) { /* 0x448D0 */
        return false;
    }
    return W(pDat, &pSt->nUnk138E70, 4); /* 0x448E4 */
}

bool Save_ReadRmp(DatFile *pDat, RmpData *pRmp, RmpModuleLoad pfnLoad, void *pUser)
{
    i32 aTileX[PUFO_SAVE_RMP_MODULE_MAX];
    i32 aTileY[PUFO_SAVE_RMP_MODULE_MAX];
    i32 aSizeX[PUFO_SAVE_RMP_MODULE_MAX];
    i32 aSizeY[PUFO_SAVE_RMP_MODULE_MAX];
    i32 aLevel[PUFO_SAVE_RMP_MODULE_MAX];
    char aNames[PUFO_SAVE_RMP_MODULE_MAX][20];
    i32 nCount;

    if (pDat == NULL || pRmp == NULL || pfnLoad == NULL) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_RMP)) { /* 0x43EF4 */
        return false;
    }
    if (!DatFile_Read(pDat, &nCount, 4)) { /* 0x43F04 */
        return false;
    }
    if (nCount > PUFO_SAVE_RMP_MODULE_MAX) { /* deviation: the original has no bound check */
        return false;
    }

    /* 0x43F18..0x43F7C: the six blocks, in the original's read order. */
    if (!DatFile_Read(pDat, aTileX, sizeof(aTileX))) { return false; }
    if (!DatFile_Read(pDat, aTileY, sizeof(aTileY))) { return false; }
    if (!DatFile_Read(pDat, aSizeX, sizeof(aSizeX))) { return false; }
    if (!DatFile_Read(pDat, aSizeY, sizeof(aSizeY))) { return false; }
    if (!DatFile_Read(pDat, aNames, sizeof(aNames))) { return false; }
    if (!DatFile_Read(pDat, aLevel, sizeof(aLevel))) { return false; }

    if (nCount <= 0) { /* 0x43F8C: a zero or negative count loads nothing */
        return true;
    }

    for (i32 i = 0; i < nCount; ++i) { /* 0x43F9C..0x43FFC */
        (void)pfnLoad(pRmp, aNames[i], aTileX[i], aTileY[i], aSizeX[i], aSizeY[i],
                      aLevel[i], pUser);
    }
    return true;
}

/* Bounded concatenation. The original uses strcat with no bound at all; the port caps
 * the append so a corrupt save cannot write past the 40-byte line. */
static void StrAppend(char *pszDst, size_t cbDst, const char *pszText)
{
    size_t nUsed = strlen(pszDst);
    size_t nLen;

    if (pszText == NULL || nUsed + 1 >= cbDst) {
        return;
    }
    nLen = strlen(pszText);
    if (nLen > cbDst - 1 - nUsed) {
        nLen = cbDst - 1 - nUsed;
    }
    memcpy(pszDst + nUsed, pszText, nLen);
    pszDst[nUsed + nLen] = '\0';
}

static void StrAppendInt(char *pszDst, size_t cbDst, i32 nValue)
{
    char szNum[16];

    snprintf(szNum, sizeof(szNum), "%d", nValue);
    StrAppend(pszDst, cbDst, szNum);
}

void SaveSlot_FormatList(SaveSlotFormatCtx *pCtx)
{
    DatFile dat;
    char szPath[296]; /* pszPath @ bp-15Ch */
    i32 nTotalSecSaved;

    if (pCtx == NULL || pCtx->apLines == NULL || pCtx->pTime == NULL ||
        pCtx->pbGameState == NULL) {
        return;
    }

    DatFile_ctor(&dat); /* 0x45914 */

    /* 0x4593C: nTotalSec is saved so the live clock can be put back at the end.
     * The decoded fields are NOT put back. */
    nTotalSecSaved = pCtx->pTime->nTotalSec;

    for (i32 nSlot = 0; nSlot < PUFO_SAVE_SLOT_COUNT; ++nSlot) { /* 0x45C04 */
        char *pszLine = pCtx->apLines[nSlot];

        pszLine[0] = '\0';

        /* 0x45944..0x45994: <dir> + "sav" + itoa(slot) + ".sav". */
        szPath[0] = '\0';
        StrAppend(szPath, sizeof(szPath), pCtx->pszSaveDir);
        StrAppend(szPath, sizeof(szPath), "sav");
        StrAppendInt(szPath, sizeof(szPath), nSlot);
        StrAppend(szPath, sizeof(szPath), ".sav");

        if (!DatFile_Exists(szPath)) { /* 0x459A8 */
            StrAppend(pszLine, PUFO_SAVE_SLOT_LINE_SIZE, pCtx->pszNotSaved);
            continue;
        }

        DatFile_Open(&dat, szPath, PUFO_DAT_READ, 100); /* 0x459BC */

        /* 0x459C8..0x459E0: the results are not checked. A save with no time chunk
         * formats whatever nTotalSec already held. */
        (void)DatFile_FindChunk(&dat, PUFO_SAVE_TAG_TIME);
        (void)DatFile_Read(&dat, &pCtx->pTime->nTotalSec, 4);
        (void)GameTime_Decode(pCtx->pTime);

        /* 0x459E4: aTextRecs[205 * 1800], out of range in this data set, so this
         * only writes the terminator. */
        StrAppend(pszLine, PUFO_SAVE_SLOT_LINE_SIZE, pCtx->pszPrefix);

        /* 0x45A00..0x45AF4: the separator doubles as the pad character. */
        if (pCtx->pTime->nDay < 10) {
            StrAppend(pszLine, PUFO_SAVE_SLOT_LINE_SIZE, "0");
        }
        StrAppendInt(pszLine, PUFO_SAVE_SLOT_LINE_SIZE, pCtx->pTime->nDay);
        StrAppend(pszLine, PUFO_SAVE_SLOT_LINE_SIZE,
                  pCtx->pTime->nMonth >= 10 ? "." : ".0");
        StrAppendInt(pszLine, PUFO_SAVE_SLOT_LINE_SIZE, pCtx->pTime->nMonth);
        StrAppend(pszLine, PUFO_SAVE_SLOT_LINE_SIZE, ".");
        StrAppendInt(pszLine, PUFO_SAVE_SLOT_LINE_SIZE, pCtx->pTime->nYear);
        StrAppend(pszLine, PUFO_SAVE_SLOT_LINE_SIZE,
                  pCtx->pTime->nHour >= 10 ? " " : " 0");
        StrAppendInt(pszLine, PUFO_SAVE_SLOT_LINE_SIZE, pCtx->pTime->nHour);
        StrAppend(pszLine, PUFO_SAVE_SLOT_LINE_SIZE,
                  pCtx->pTime->nMinute >= 10 ? ":" : ":0");
        StrAppendInt(pszLine, PUFO_SAVE_SLOT_LINE_SIZE, pCtx->pTime->nMinute);

        /* 0x45B00..0x45B1C: the flag says a tactical mission is stored in the file. */
        (void)DatFile_FindChunk(&dat, PUFO_SAVE_TAG_GAMESTATE);
        (void)DatFile_Read(&dat, pCtx->pbGameState, 1);

        if (*pCtx->pbGameState != 0) {
            StrAppend(pszLine, PUFO_SAVE_SLOT_LINE_SIZE, pCtx->pszMission);

            (void)DatFile_FindChunk(&dat, PUFO_SAVE_TAG_MCNT); /* 0x45B3C */
            if (DatFile_ChunkPresent(&dat)) { /* 0x45B4C */
                i32 nMissionCount = 0;

                (void)DatFile_Read(&dat, &nMissionCount, 1);
                StrAppend(pszLine, PUFO_SAVE_SLOT_LINE_SIZE, " ");
                StrAppendInt(pszLine, PUFO_SAVE_SLOT_LINE_SIZE,
                             (i32)((u8)nMissionCount));
            }
        }

        DatFile_Close(&dat); /* 0x45B8C */
    }

    /* 0x45C10: only the total goes back. The decoded date stays in the clock. */
    pCtx->pTime->nTotalSec = nTotalSecSaved;
    DatFile_dtor(&dat); /* 0x45C14 */
}

bool Save_WriteStrateg(DatFile *pDat, const StrategSaveState *pSt, const PtrArray *pList)
{
    if (pDat == NULL || pSt == NULL || pList == NULL) {
        return false;
    }
    if (pSt->pnUnk1865A8 == NULL || pSt->pnUnk186594 == NULL) {
        /* The original hands these two globals to DatFile_Write as addresses. */
        return false;
    }
    if (!DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_STRATEG)) { /* 0x3FBC0 */
        return false;
    }

    /* 0x3FBD4..0x3FE24: the 38 header writes, in the original's order. */
    if (!W(pDat, &pSt->bSaveVersion, 1)) { return false; }
    if (!W(pDat, &pSt->nUnk186558, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk186550, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk186554, 4)) { return false; }
    /* 0x3FC14 writes MEMORY[0x186530]. That is NOT the PtrArray's own count field: the
     * strateg PtrArray starts at 0x186534, so its count is at 0x186534 and its item array
     * at 0x186538. 0x186530 is a separate global that mirrors the item count, and it is
     * also the loop bound. */
    if (!W(pDat, &pSt->nStrategCount, 4)) { return false; }
    if (!W(pDat, &pSt->abTypeCounters[0], 1)) { return false; }
    if (!W(pDat, &pSt->abTypeCounters[1], 1)) { return false; }
    if (!W(pDat, &pSt->abTypeCounters[2], 1)) { return false; }
    if (!W(pDat, &pSt->abTypeCounters[3], 1)) { return false; }
    if (!W(pDat, &pSt->abTypeCounters[4], 1)) { return false; }
    if (!W(pDat, &pSt->nUnk186544, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk186548, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk18654C, 4)) { return false; }
    if (!W(pDat, &pSt->nUnkMap6691p4845, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk1865BC, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk1865B4, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk1865A0, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk18658C, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk186574, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk1865B8, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk1865A4, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk186590, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk186578, 4)) { return false; }
    if (!W(pDat, pSt->pnUnk1865A8, 4)) { return false; } /* 0x3FD44, a POINTER */
    if (!W(pDat, pSt->pnUnk186594, 4)) { return false; } /* 0x3FD54, a POINTER */
    if (!W(pDat, &pSt->nUnk186580, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk186568, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk1865B0, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk18659C, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk186588, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk186570, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk1865AC, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk186598, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk186584, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk18656C, 4)) { return false; }
    if (!W(pDat, &pSt->nUnk186BE4, 4)) { return false; }
    if (!W(pDat, &pSt->nUnkMap6691p6937, 4)) { return false; }
    if (!W(pDat, &pSt->nSelectedShipIdx, 4)) { return false; }

    for (i32 i = 0; i < pSt->nStrategCount; ++i) { /* 0x3FE34 */
        const StrategItem *p = (const StrategItem *)PtrArray_Get(pList, i);

        if (p == NULL) {
            return false;
        }
        /* 0x3FE50..0x401D0: 33 fields, in the original's order. The entry at +0 is written
         * twelfth into the record, not first. */
        if (!W(pDat, &p->bActive, 1)) { return false; }
        if (!W(pDat, &p->nSiteId, 4)) { return false; }
        if (!W(pDat, &p->bRandA, 1)) { return false; }
        if (!W(pDat, &p->nTimer, 4)) { return false; }
        if (!W(pDat, &p->nSeed, 4)) { return false; }
        if (!W(pDat, &p->bVisible, 1)) { return false; }
        if (!W(pDat, &p->bFlagA, 1)) { return false; }
        if (!W(pDat, &p->nDisplayId, 4)) { return false; }
        if (!W(pDat, &p->bCraftClass, 1)) { return false; }
        if (!W(pDat, &p->nX, 4)) { return false; }
        if (!W(pDat, &p->nDestX, 4)) { return false; }
        if (!W(pDat, &p->nStartX, 4)) { return false; }
        if (!W(pDat, &p->nY, 4)) { return false; }
        if (!W(pDat, &p->nDestY, 4)) { return false; }
        if (!W(pDat, &p->nStartY, 4)) { return false; }
        if (!W(pDat, &p->nMissionSub, 1)) { return false; }
        if (!W(pDat, &p->nTravelStartSec, 4)) { return false; }
        if (!W(pDat, &p->dSpeed, 8)) { return false; }
        if (!W(pDat, &p->dTrackA, 8)) { return false; }
        if (!W(pDat, &p->bHyperDetect, 1)) { return false; }
        if (!W(pDat, &p->nType, 1)) { return false; } /* 0x40080, the item pointer itself */
        if (!W(pDat, &p->bTravelDir, 1)) { return false; }
        if (!W(pDat, &p->uTravelMin, 2)) { return false; }
        if (!W(pDat, &p->nTravelEpochSec, 4)) { return false; }
        if (!W(pDat, &p->dFracX, 8)) { return false; }
        if (!W(pDat, &p->dFracY, 8)) { return false; }
        if (!W(pDat, &p->nHp, 4)) { return false; }
        if (!W(pDat, &p->nPhase, 4)) { return false; }
        if (!W(pDat, &p->bRace, 1)) { return false; }
        if (!W(pDat, &p->nTargetIdx, 4)) { return false; }
        if (!W(pDat, &p->bDone, 1)) { return false; }
        /* The writer has NO version gate: nTargetX and nTargetY are always written. */
        if (!W(pDat, &p->nTargetX, 4)) { return false; }
        if (!W(pDat, &p->nTargetY, 4)) { return false; }
    }
    return true;
}

bool Save_ReadStrateg(DatFile *pDat, StrategSaveState *pSt, PtrArray *pList)
{
    i8 bVersionFromDisk = 0;
    bool bFound;

    if (pDat == NULL || pSt == NULL || pList == NULL) {
        return false;
    }

    bFound = DatFile_FindChunk(pDat, PUFO_SAVE_TAG_STRATEG); /* 0x40F5C */

    /* 0x40F68: the list is emptied BEFORE anything is read, and FindChunk's result is not
     * checked, so a save with no strateg chunk still clears the live list. */
    PtrArray_DeleteAll(pList);

    /* 0x40F78: the version byte is read into a scratch and DISCARDED. The per-item gate below
     * tests pSt->bSaveVersion, which is the build's own 0x18655C. */
    (void)DatFile_Read(pDat, &bVersionFromDisk, 1);

    (void)DatFile_Read(pDat, &pSt->nUnk186558, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk186550, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk186554, 4);
    (void)DatFile_Read(pDat, &pSt->nStrategCount, 4);
    (void)DatFile_Read(pDat, &pSt->abTypeCounters[0], 1);
    (void)DatFile_Read(pDat, &pSt->abTypeCounters[1], 1);
    (void)DatFile_Read(pDat, &pSt->abTypeCounters[2], 1);
    (void)DatFile_Read(pDat, &pSt->abTypeCounters[3], 1);
    (void)DatFile_Read(pDat, &pSt->abTypeCounters[4], 1);
    (void)DatFile_Read(pDat, &pSt->nUnk186544, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk186548, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk18654C, 4);
    (void)DatFile_Read(pDat, &pSt->nUnkMap6691p4845, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk1865BC, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk1865B4, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk1865A0, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk18658C, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk186574, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk1865B8, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk1865A4, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk186590, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk186578, 4);
    /* 0x410E8/0x410F8: these two header fields are read as POINTERS into the globals
     * 0x1865A8 and 0x186594, and the reads are NEVER conditional — skipping one would
     * desynchronise the whole rest of the chunk. The port models the globals as caller
     * pointers; when one is NULL the 4 bytes still go to a scratch so the stream stays
     * aligned. */
    {
        i32 nScratch;
        (void)DatFile_Read(pDat, pSt->pnUnk1865A8 != NULL ? pSt->pnUnk1865A8 : &nScratch, 4);
        (void)DatFile_Read(pDat, pSt->pnUnk186594 != NULL ? pSt->pnUnk186594 : &nScratch, 4);
    }
    (void)DatFile_Read(pDat, &pSt->nUnk186580, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk186568, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk1865B0, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk18659C, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk186588, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk186570, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk1865AC, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk186598, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk186584, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk18656C, 4);
    (void)DatFile_Read(pDat, &pSt->nUnk186BE4, 4);
    (void)DatFile_Read(pDat, &pSt->nUnkMap6691p6937, 4);
    (void)DatFile_Read(pDat, &pSt->nSelectedShipIdx, 4);

    /* 0x411DC..0x415A4: a do/while on the count that was just read. The list itself was
     * emptied above and is rebuilt by StrategItem_AllocAdd, so the loop bound is the
     * separate 0x186530 global and not the PtrArray's own count. */
    for (i32 i = 0; i < pSt->nStrategCount; ++i) {
        StrategItem *p;

        if (!StrategItem_AllocAdd(pList, 0)) { /* 0x411E4 */
            return false;
        }
        p = (StrategItem *)PtrArray_Get(pList, i);
        if (p == NULL) {
            return false;
        }

        (void)DatFile_Read(pDat, &p->bActive, 1);
        (void)DatFile_Read(pDat, &p->nSiteId, 4);
        (void)DatFile_Read(pDat, &p->bRandA, 1);
        (void)DatFile_Read(pDat, &p->nTimer, 4);
        (void)DatFile_Read(pDat, &p->nSeed, 4);
        (void)DatFile_Read(pDat, &p->bVisible, 1);
        (void)DatFile_Read(pDat, &p->bFlagA, 1);
        (void)DatFile_Read(pDat, &p->nDisplayId, 4);
        (void)DatFile_Read(pDat, &p->bCraftClass, 1);
        (void)DatFile_Read(pDat, &p->nX, 4);
        (void)DatFile_Read(pDat, &p->nDestX, 4);
        (void)DatFile_Read(pDat, &p->nStartX, 4);
        (void)DatFile_Read(pDat, &p->nY, 4);
        (void)DatFile_Read(pDat, &p->nDestY, 4);
        (void)DatFile_Read(pDat, &p->nStartY, 4);
        (void)DatFile_Read(pDat, &p->nMissionSub, 1);
        (void)DatFile_Read(pDat, &p->nTravelStartSec, 4);
        (void)DatFile_Read(pDat, &p->dSpeed, 8);
        (void)DatFile_Read(pDat, &p->dTrackA, 8);
        (void)DatFile_Read(pDat, &p->bHyperDetect, 1);
        (void)DatFile_Read(pDat, &p->nType, 1); /* 0x41430, the item pointer itself */
        (void)DatFile_Read(pDat, &p->bTravelDir, 1);
        (void)DatFile_Read(pDat, &p->uTravelMin, 2);
        (void)DatFile_Read(pDat, &p->nTravelEpochSec, 4);
        (void)DatFile_Read(pDat, &p->dFracX, 8);
        (void)DatFile_Read(pDat, &p->dFracY, 8);
        (void)DatFile_Read(pDat, &p->nHp, 4);
        (void)DatFile_Read(pDat, &p->nPhase, 4);
        (void)DatFile_Read(pDat, &p->bRace, 1);
        (void)DatFile_Read(pDat, &p->nTargetIdx, 4);
        (void)DatFile_Read(pDat, &p->bDone, 1);

        /* 0x41554: the gate is on the BUILD's version byte, not the one this file carried. */
        if (pSt->bSaveVersion > 2) {
            (void)DatFile_Read(pDat, &p->nTargetX, 4);
            (void)DatFile_Read(pDat, &p->nTargetY, 4);
        }
    }

    return bFound;
}

/* SaveGame @ 0x44988. The whole slot file, in the original's order. */
bool SaveGame(DatFile *pDat, const SaveGameInput *pIn)
{
    char pszPath[296];   /* the original's pszPath[292] plus the itoa scratch */
    bool bOk;

    if (pDat == NULL || pIn == NULL) {
        return false;
    }

    /* 0x4499C: a frame is rendered before the file is opened, so the save lands on
     * a settled screen rather than mid-update. */
    if (pIn->pfnRenderFrame != NULL) {
        pIn->pfnRenderFrame(pIn->pRenderUser);
    }

    if (!Save_BuildSlotPath(pszPath, sizeof(pszPath), pIn->pszAppDir, pIn->nSlot)) {
        return false;
    }
    if (!DatFile_Open(pDat, pszPath, PUFO_DAT_CREATE, 100)) { /* 0x44A04 */
        return false;
    }

    bOk = Save_WriteVersion(pDat);                     /* tactver, 0x44A10..0x44A28 */
    if (bOk) { bOk = Save_WriteGameState(pDat, pIn->bTactFlag); }          /* 0x44A2C */
    /* 0x44A30: the roster ids are assigned here, between gamestate and bases, so the
     * flobjs links written later can be stored as indices. The original ignores the
     * result, and this helper reports false when the tactical flag is clear, so its
     * result must NOT gate the rest of the save. */
    (void)Save_AssignRosterIndices(pIn->bTactFlag != 0, pIn->apSoldiers,
                                   pIn->nSoldierCount, pIn->apTactItems,
                                   pIn->nTactItemCount);
    if (bOk) {
        /* The original's Save_WriteBases reaches its globals as it writes them. The port reads a record
         * instead, so the live cells are staged into it right here (0x3F21C..0x3F3A4). */
        Save_BasesLiveToRecord(pIn->pBasesLive, pIn->pBases);
        bOk = Save_WriteBases(pDat, pIn->pBases, pIn->apBases,          /* 0x44A34 */
                              pIn->apObjects, pIn->nObjectCount);
    }
    if (bOk) { bOk = Save_WriteProduct(pDat, pIn->pProductBlock); }              /* 0x44A38 */
    if (bOk) { bOk = Save_WriteResearch(pDat, pIn->pResearch); }                /* 0x44A3C */
    if (bOk) {
        bOk = Save_WriteShips(pDat, pIn->apShips, pIn->nShipCount,     /* 0x44A40 */
                              pIn->apObjects, pIn->nObjectCount);
    }
    if (bOk) { bOk = Save_WriteSmap(pDat, pIn->pSmap); }                        /* 0x44A44 */
    if (bOk) { bOk = Save_WriteStrateg(pDat, pIn->pStrateg, pIn->pStrategList); } /* 0x44A48 */
    if (bOk) { bOk = Save_WriteTime(pDat, pIn->pTime); }                        /* 0x44A4C */
    if (bOk) {
        /* The rosters go along because the writer needs them for the bInTact computation (0x423C0..0x424B8)
         * that the stactflg chunk and the record's cell arm both carry. */
        bOk = Save_WriteSoldiers(pDat, pIn->apSoldiers, pIn->nSoldierCount,
                                 pIn->bTactFlag, pIn->pRosters);                /* 0x44A50 */
    }

    if (pIn->bTactFlag != 0) {
        /* 0x44A80: the mission counter is bumped HERE, and the value written is the
         * incremented one. Saving a mission advances it. */
        if (pIn->pnMissionCount != NULL) {
            (*pIn->pnMissionCount)++;
        }
        if (bOk) { bOk = DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_MCNT); }        /* 0x44A88 */
        if (bOk) { bOk = W(pDat, pIn->pnMissionCount, 1); }                     /* 0x44AA0 */
        if (bOk) {
            /* 0x44AA4: this one call writes tgamedata, teamunits, aliens, diedsolds,
             * hiddensold, map and v124.dat. */
            bOk = Save_WriteTGameData(pDat, pIn->pTact, pIn->pRosters, pIn->pFlobjs,
                                      pIn->pMap, pIn->pfnLookup, pIn->pLookupUser);
        }
        if (bOk) {
            bOk = Save_WriteObjBefore(pDat, pIn->apObjects, pIn->nObjectCount);  /* 0x44AAC */
        }
        /* 0x44AB8..0x44B78: the rmp chunk, written inline because no writer function
         * for it exists anywhere else. */
        if (bOk) { bOk = DatFile_BeginChunk(pDat, PUFO_SAVE_TAG_RMP); }
        if (bOk) { bOk = W(pDat, &pIn->pRmp->nModCount, 4); }
        if (bOk) { bOk = W(pDat, pIn->pRmp->anModTileX, 0xF0u); }
        if (bOk) { bOk = W(pDat, pIn->pRmp->anModTileY, 0xF0u); }
        if (bOk) { bOk = W(pDat, pIn->pRmp->anModSizeX, 0xF0u); }
        if (bOk) { bOk = W(pDat, pIn->pRmp->anModSizeY, 0xF0u); }
        if (bOk) { bOk = W(pDat, pIn->pRmp->aszModName, 0x4B0u); }
        if (bOk) { bOk = W(pDat, pIn->pRmp->anModLevel, 0xF0u); }
        /* 0x44B80: only the tactical branch closes the screen. */
        if (pIn->pnUiScreen != NULL) {
            *pIn->pnUiScreen = 0;
        }
    }

    DatFile_Close(pDat);   /* 0x44B88: closed even after a failed write */
    return bOk;
}

/* Save_ReadTGameData @ 0x44248. */
bool Save_ReadTGameData(DatFile *pDat, TactSaveState *pSt,
                        TeamUnitsState *pRosters, const FlobjsReadState *pFlobjs,
                        MapGeom *pMap, MapCellLookup pfnLookup, void *pLookupUser,
                        TactResetState *pReset, const TactLoadHooks *pHooks)
{
    bool bOk = true;

    if (pDat == NULL || pSt == NULL || pRosters == NULL || pFlobjs == NULL ||
        pMap == NULL || pReset == NULL || pHooks == NULL) {
        return false;
    }

    /* 0x44258: the original does not check FindChunk, and checks none of these reads
     * either. The port keeps the same order and reports the failures. */
    (void)DatFile_FindChunk(pDat, PUFO_SAVE_TAG_TGAMEDATA);
    if (!DatFile_Read(pDat, &pSt->bUnk15110, 1))       { bOk = false; }
    if (!DatFile_Read(pDat, &pSt->nUnk1510C, 4))       { bOk = false; }
    if (!DatFile_Read(pDat, &pSt->abUnk15111[0], 1))   { bOk = false; }
    if (!DatFile_Read(pDat, &pSt->abUnk15111[1], 1))   { bOk = false; }
    if (!DatFile_Read(pDat, &pSt->abUnk15111[2], 1))   { bOk = false; }
    if (!DatFile_Read(pDat, &pSt->nMapLayers, 1))      { bOk = false; }
    if (!DatFile_Read(pDat, &pSt->bUnk15115, 1))       { bOk = false; }
    if (!DatFile_Read(pDat, pSt->abSpriteSet, 4))      { bOk = false; }
    if (!DatFile_Read(pDat, &pSt->nMissionType, 1))    { bOk = false; }
    if (!DatFile_Read(pDat, &pSt->bMissionMode, 1))    { bOk = false; }
    if (!DatFile_Read(pDat, &pSt->bUnk1511E, 1))       { bOk = false; }
    for (i32 i = 0; i < 4; ++i) {
        if (!DatFile_Read(pDat, &pSt->anUnk15188[i], 4)) { bOk = false; }
    }
    for (i32 i = 0; i < 3; ++i) {
        if (!DatFile_Read(pDat, &pSt->abUnk15198[i], 1)) { bOk = false; }
    }
    if (!DatFile_Read(pDat, &pSt->nUnk151A0, 4))        { bOk = false; }
    for (i32 i = 0; i < 3; ++i) {
        /* The craft tiles are dwords in memory and one byte each on disk. */
        if (!DatFile_Read(pDat, &pSt->anCraftTile[i], 1)) { bOk = false; }
    }

    /* The original writes these 23 fields straight into the Map block, and the very next call
     * reads them back from there (Font_LoadTactSprites reaches Map_nMapLayers @ 0x15114). The port
     * stages them in pSt, so the caller's apply hook is the point where they become live. */
    if (pHooks->pfnApplyState != NULL) {
        pHooks->pfnApplyState(pSt, pHooks->pUser);
    }

    /* 0x4444C: Font_LoadTactSprites @ 0x257c4, ported in pufo_font.c and installed by the caller. */
    if (pHooks->pfnLoadTactSprites != NULL) {
        pHooks->pfnLoadTactSprites(pHooks->pUser);
    }

    /* 0x44450..0x4449C: the four sprite-set bytes at Map+0x15116. A slot of 23 skips the whole
     * body (0x44468/0x44470), so it loads nothing and leaves its base word alone. On every other
     * pass 0x44474 reads fontSys_nTotalSprFrames FRESH and 0x4447C stores it into
     * fontSys_anAlienSprBase[i] before 0x44488's Font_LoadSpr moves that counter -- so each slot
     * records the running counter the PREVIOUS set left, exactly like Tact_SpawnAliens does on the
     * build path. */
    for (i32 i = 0; i < 4; ++i) {
        if (pSt->abSpriteSet[i] != PUFO_SPRITE_SET_NONE) {
            if (pHooks->pFont != NULL) {
                pHooks->pFont->anAlienSprBase[i] = pHooks->pFont->nTotalSprFrames;
            }
            if (pHooks->pfnLoadSpr != NULL) {
                (void)pHooks->pfnLoadSpr(pSt->abSpriteSet[i], pHooks->pUser);
            }
        }
    }
    if (pHooks->pfnBuildIndex != NULL) {   /* 0x444A4 */
        pHooks->pfnBuildIndex(pHooks->pUser);
    }

    /* 0x444A8, 0x444B0, 0x444B4: the three chunk readers, in this order. */
    (void)Save_ReadTeamUnits(pDat, pRosters, pFlobjs->apSoldiers, *pFlobjs->pnSoldierCount);
    if (pHooks->pfnApplyRosters != NULL) {
        pHooks->pfnApplyRosters(pRosters, pHooks->pUser);
    }
    (void)Save_ReadFlobjs(pDat, pFlobjs->nWideCountFlag, pFlobjs->pnTactItemCount,
                          pFlobjs->apTactItems, pFlobjs->apObjects, pFlobjs->nObjectCount,
                          pFlobjs->apSoldiers);
    (void)Save_ReadMap(pDat, pMap, pfnLookup, pLookupUser,
                       pFlobjs->apSoldiers, *pFlobjs->pnSoldierCount,
                       pFlobjs->apTactItems, *pFlobjs->pnTactItemCount, pHooks->pFont);

    /* 0x444C0..0x445CC: the tactical reset. Mostly clears, but two are sets. */
    pReset->abGap01[0] = 0;
    pReset->abGap01[1] = 0;
    pReset->nMultiTargetCount = 0;
    pReset->nUnk2C = 0;
    pReset->nAnimPhase = 0;
    pReset->bPlayerInputEnabled = 1;   /* a SET, not a clear */
    pReset->bUnk3C = 0;
    pReset->abFire[0] = 0;
    pReset->abFire[1] = 0;
    pReset->abFire[2] = 0;
    pReset->abFire[3] = 0;             /* the original clears 4..7 as one dword */
    pReset->abFire[4] = 0;
    pReset->abFire[5] = 0;
    pReset->abFire[6] = 0;
    pReset->abFire[7] = 0;
    pReset->abFire[8] = 0;
    pReset->abFire[9] = 0;
    pReset->bGameTickHold = 0;
    pReset->bEnemyTurnActive = 0;
    pReset->bBattleEnded = 0;
    pReset->aUiSlot[0] = 0;
    pReset->aUiSlot[3] = 0;
    pReset->aUiSlot[4] = 200;          /* a value in a pointer slot */
    pReset->aUiSlot[5] = 0;
    pReset->aUiSlot[6] = 0;
    pReset->aUiSlot[7] = 0;
    pReset->aUiSlot[8] = 0;            /* written as two half-writes in the original */
    pReset->aUiSlot[9] = 0;
    pReset->aUiSlot[10] = 10;          /* and another value */
    pReset->nUiScreen = 0;
    if (pHooks->pbUnk157198Flag != NULL) {   /* 0x445AC, through the object-blob pointer */
        *pHooks->pbUnk157198Flag = 0;
    }
    if (pHooks->pfnApplyReset != NULL) {
        pHooks->pfnApplyReset(pReset, pHooks->pUser);
    }

    /* 0x445D4..0x446A0: v124.dat is optional, and when it is missing the two dwords
     * are ROLLED rather than left alone. */
    (void)DatFile_FindChunk(pDat, PUFO_SAVE_TAG_V124);
    if (DatFile_ChunkPresent(pDat)) {
        if (!DatFile_Read(pDat, &pSt->nUnk138E6C, 4)) { bOk = false; }
        if (!DatFile_Read(pDat, &pSt->nUnk138E70, 4)) { bOk = false; }
    } else {
        if (pHooks->pfnRandU16 != NULL) {
            pSt->nUnk138E6C = (i32)(u16)pHooks->pfnRandU16(0x28u, pHooks->pUser) + 10;
            pSt->nUnk138E70 = (i32)(u16)pHooks->pfnRandU16(0x14u, pHooks->pUser);
        }
    }
    return bOk;
}

/* The research-unlock switch's topic -> base-facility-flag map, read off the jumptable at
 * 0x4539C: topics 0..16 in order, -1 for topic 10 (the jumptable's default case). */
static const i8 s_abResearchUnlockFlag[17] = {
    0, 1, 2, 3, 4, 5, 13, 6, 7, 14, -1, 15, 16, 12, 10, 8, 9
};

i32 LoadGame(DatFile *pDat, LoadGameEnv *pEnv)
{
    const LoadGameHooks *pHooks = pEnv->pHooks;
    char   szPath[296];
    i32    nVer;
    i32    nCraftCount;
    i32    nBaseCount;
    i32    nSiteCount;
    bool   bOk = true;

    if (pDat == NULL || pHooks == NULL) {
        return 0;
    }
    if (!Save_BuildSlotPath(szPath, sizeof(szPath), pEnv->pszAppDir, pEnv->nSlot)) {
        return 0;
    }
    if (!DatFile_Exists(szPath)) { /* 0x44C38: no file, no load, no teardown */
        return 0;
    }

    if (pHooks->pfnRenderFrame != NULL) { /* 0x44C44 */
        pHooks->pfnRenderFrame(pHooks->pUser);
    }
    if (!DatFile_Open(pDat, szPath, PUFO_DAT_READ, 100)) { /* 0x44C58: 100 entries */
        return 0;
    }

    /* 0x44C64..0x44C94: tactver. The 2 bytes are read into a zero-initialised dword and the
     * VALUE is stored at slot-context + 0x118 — R7 is &MainMenu_pSlotCtx @ 0x156938 (every
     * caller passes it), so the dword lands right after the 7x40-byte line buffer at 0x156A50.
     * Nothing in the binary reads that dword (the only 0x118 access on this base is the store),
     * so it is write-only residue; the port hands the value out through pnTactVerOut. */
    (void)DatFile_FindChunk(pDat, PUFO_SAVE_TAG_TACTVER);
    {
        u32 nTactVer = 0;
        if (DatFile_ChunkPresent(pDat)) {
            (void)DatFile_Read(pDat, &nTactVer, 2); /* 0x44C8C: 2 bytes into the low half */
        }
        if (pEnv->pnTactVerOut != NULL) {
            *pEnv->pnTactVerOut = nTactVer; /* 0x44C94 */
        }
    }

    if (!Save_ReadGameState(pDat, pEnv->pbGameState)) { bOk = false; } /* 0x44C98 */

    /* 0x44CB0: loading from the start menu starts from an empty world. */
    if (*pEnv->puUiScreen == 17) {
        if (pHooks->pfnStartUiShutdown != NULL) { pHooks->pfnStartUiShutdown(pHooks->pUser); }
        if (pHooks->pfnGeoscapeInitUi != NULL)   { pHooks->pfnGeoscapeInitUi(1, pHooks->pUser); }
        *(u16 *)pEnv->pnSoldierCount = 0;        /* 0x44CCC: STRH, the LOW half only */
        pEnv->pBases->bBaseCount = 0;            /* 0x44CD4 */
        *pEnv->pbCraftCount = 0;                 /* 0x44CE4 */
        pEnv->pStrateg->nStrategCount = 0;       /* 0x44CEC */
    }

    /* 0x44D08..0x44D80: the three-way teardown. The combination bGeoscapeUi && !bGameState
     * frees NOTHING — that is the original's own path, reproduced. */
    if (*pEnv->pbTactFlag != 0 && *pEnv->pbGameState == 0) {
        if (pHooks->pfnTactShutdown != NULL)      { pHooks->pfnTactShutdown(pHooks->pUser); }
        if (pHooks->pfnGeoscapeInitUi != NULL)    { pHooks->pfnGeoscapeInitUi(1, pHooks->pUser); }
    } else if (*pEnv->pbGeoscapeUi != 0) {
        if (*pEnv->pbGameState != 0 && pHooks->pfnGeoscapeUiFreeAll != NULL) {
            pHooks->pfnGeoscapeUiFreeAll(pHooks->pUser);
        }
    } else {
        if (pHooks->pfnTactShutdown != NULL) { pHooks->pfnTactShutdown(pHooks->pUser); }
    }

    /* 0x44D84..0x44DB8: the version byte comes from the strateg chunk BEFORE any chunk body
     * is read, because it sizes the other readers. */
    if (!Save_ReadStrategVersion(pDat, &pEnv->pStrateg->bSaveVersion)) { bOk = false; }
    nVer = (i32)pEnv->pStrateg->bSaveVersion;

    if (!Save_ReadBases(pDat, nVer, pEnv->pBases, pEnv->apBases, pEnv->nOldBaseCount,
                        (u8)pEnv->pStrateg->nUnk186558, pEnv->apObjects, pEnv->nObjectCount)) { bOk = false; }
    /* The original's Save_ReadBases writes its globals as it reads them, so everything downstream of this
     * chunk sees the loaded values -- the per-base Base_ClearFacilityGridRow classification at
     * 0x44E38..0x44E7C reads the grid itself. The port's reader fills the record, so the join runs here. */
    Save_BasesRecordToLive(pEnv->pBases, pEnv->pBasesLive);
    if (!Save_ReadProduct(pDat, nVer, pEnv->pProductBlock)) { bOk = false; }
    if (!Save_ReadResearch(pDat, pEnv->pResearch)) { bOk = false; }

    nCraftCount = (i32)*pEnv->pbCraftCount; /* in/out: the count held before the read frees the old array */
    if (!Save_ReadShips(pDat, pEnv->apCraft, &nCraftCount, pEnv->pShipEnv)) { bOk = false; }
    *pEnv->pbCraftCount = (u8)nCraftCount;

    if (!Save_ReadSmap(pDat, pEnv->pSmap, pEnv->pView, pEnv->paZoomSets,
                       pEnv->pWorldSize, *pEnv->pbGameState != 0)) { bOk = false; }
    /* 0x40D94/0x40DAC: the smap chunk also restores the strateg seed counter (0x18640C) and the
     * site count (0x186410, held behind a pointer in the original). The port keeps both in the
     * wire record; hand the seed to the caller's counter and take the count for the budget passes. */
    *pEnv->pnSeed = pEnv->pSmap->nParam3;
    nSiteCount = pEnv->pSmap->nParam4;
    if (!Save_ReadStrateg(pDat, pEnv->pStrateg, pEnv->pStrategList)) { bOk = false; }
    if (!Save_ReadTime(pDat, pEnv->pTimeRec)) { bOk = false; }
    /* The original reads the time chunk straight into the live clock block (apLines[101..115]);
     * the port reads it into the wire record and copies it over, leaving nUnkC (apLines[104],
     * never saved) alone. */
    pEnv->pTime->nLastTickMinSec = (i32)pEnv->pTimeRec->nSlot101;
    pEnv->pTime->nLastTickHourSec = (i32)pEnv->pTimeRec->nSlot102;
    pEnv->pTime->nLastTickDaySec = (i32)pEnv->pTimeRec->nSlot103;
    pEnv->pTime->nTotalSec = (i32)pEnv->pTimeRec->nSlot105;
    pEnv->pTime->bPaused = pEnv->pTimeRec->nSlot106;
    pEnv->pTime->nUnk34 = (i32)pEnv->pTimeRec->nSlot114;
    pEnv->pTime->nMonthEpochSec = (i32)pEnv->pTimeRec->nSlot115;
    if (*pEnv->pbGameState == 0) { /* 0x44DAC: soldiers only for a non-tactical save */
        /* No resolvers on this path -- and none needed. A geoscape-state save flags no soldier into a
         * mission (Save_bSoldierInTact reads the live rosters, and on the geoscape there are none), so
         * every record takes the filler arms and the cell/item/soldier id tables are never reached. The
         * original is in the same position: it resolves the three cell bytes against the cell table at
         * 0x15338, which this load has nothing allocated in, so its own result is a NULL pointer. The
         * tact flag is the stactflg-absent fallback the original takes from Map+0x00. */
        {
            SoldierRecordEnv arms;

            memset(&arms, 0, sizeof(arms));
            if (!Save_ReadSoldiers(pDat, pEnv->apSoldiers, pEnv->pnSoldierCount,
                                   pEnv->nSoldierCapacity, &arms, *pEnv->pbTactFlag)) { bOk = false; }
        }
    }

    *pEnv->pnListMode = 1; /* 0x44DC8 */

    if (*pEnv->pbGameState != 0) { /* 0x44DD8..0x44E24 */
        (void)DatFile_FindChunk(pDat, PUFO_SAVE_TAG_MCNT);
        if (DatFile_ChunkPresent(pDat)) {
            if (!DatFile_Read(pDat, pEnv->pbMissionCount, 1)) { bOk = false; }
        } else {
            *pEnv->pbMissionCount = 0;
        }
        if (pHooks->pfnTactInitMission != NULL) {
            (void)pHooks->pfnTactInitMission(1, pHooks->pUser);
        }
    }

    (void)DatFile_Close(pDat); /* 0x44E2C */
    (void)GameTime_Decode(pEnv->pTime); /* 0x44E34 */

    nBaseCount = (i32)pEnv->pBases->bBaseCount; /* signed char throughout */
    nVer       = (i32)pEnv->pStrateg->bSaveVersion;

    /* 0x44E44..0x44E7C: per-base site state. */
    for (i32 i = 0; i < nBaseCount; ++i) {
        pEnv->pWorld->apSiteState[i] =
            (u8)Base_ClearFacilityGridRow(pEnv->abFacilityGrid, i, pEnv->pWorld->apSiteState);
    }

    /* 0x44E8C..0x44F64: an old save purges unclaimed type-5 items, then teleports any active
     * craft item that is off the map to (150, 250) and gives it a mission site. */
    if (nVer < 3) {
        (void)StrategList_PurgeType5(pEnv->pStrategList, pEnv->pWorld,
                                     pEnv->pbSpawnOnceFlag, pEnv->pSpawnCtx);
        for (i32 i = 0; i < pEnv->pStrateg->nStrategCount; ++i) {
            StrategItem *pItem = PtrArray_Get(pEnv->pStrategList, i);
            if (pItem != NULL && pItem->nType == 1 && pItem->bActive == 1 &&
                (Abs32(pItem->nX) > 1000 || Abs32(pItem->nY) > 1000)) {
                pItem->nX = 150;  /* 0x44F3C */
                pItem->nY = 250;  /* 0x44F50 */
                (void)Geoscape_SpawnMissionSite(pItem, 0, pEnv->pMissionCtx); /* 0x44F64 */
            }
        }
    }

    /* 0x44F7C..0x450E8: per craft, find or create its strateg item, then the two ship flags. */
    for (i32 n = 0; n < nCraftCount; ++n) {
        Ship *pShip = pEnv->apCraft[n];
        const StrategList view = StrategViewOf(pEnv->pStrategList);
        i32   nIdx  = StrategList_FindIdxForShip(&view, pEnv->pWorld, n); /* 0x44FB0, signed char result */
        if (nIdx == -1) { /* 0x44FC0..0x45014: none — add one from the home base */
            BaseRec *pHome = (pShip != NULL && pEnv->apBases != NULL)
                                 ? pEnv->apBases[pShip->bHomeBase] : NULL;
            (void)StrategList_AddItem(pEnv->pStrategList, pEnv->pnSeed, pEnv->pView->nMode,
                                      1, 0, (pShip != NULL) ? (i32)pShip->bCraftType : 0,
                                      (pHome != NULL) ? pHome->nUnk8 : 0,
                                      (pHome != NULL) ? pHome->nUnk12 : 0,
                                      (pShip != NULL) ? (i32)pShip->bId : 0, 0);
            nIdx = pEnv->pStrategList->nCount - 1; /* 0x45014: the new last entry */
        }
        if (pShip == NULL) {
            continue; /* deviation: the original dereferences the craft slot unchecked */
        }
        StrategItem *pItem = PtrArray_Get(pEnv->pStrategList, nIdx);
        if (pShip->bWeaponsHot != 0 && pItem != NULL &&
            (Abs32(pItem->nX) > 1000 || Abs32(pItem->nY) > 1000)) { /* 0x45074..0x4508C */
            pShip->bWeaponsHot = 0;
            pShip->bAtBase     = 1;
        }
        if (pShip->bAtBase != 0 && pItem != NULL && pItem->bVisible != 0) { /* 0x450B8..0x450CC */
            pItem->bVisible = 0;
        }
    }

    /* 0x450EC..0x451D0: an old save resets out-of-range travel targets to the destination. */
    if (nVer < 3) {
        for (i32 i = 0; i < pEnv->pStrateg->nStrategCount; ++i) {
            StrategItem *pItem = PtrArray_Get(pEnv->pStrategList, i);
            if (pItem != NULL && pItem->bActive == 1 && pItem->nType == 1 &&
                (Abs32(pItem->nTargetX) > 800 || Abs32(pItem->nTargetY) > 600)) {
                pItem->nTargetX = pItem->nDestX; /* 0x451B0 */
                pItem->nTargetY = pItem->nDestY; /* 0x451D0 */
            }
        }
    }

    /* 0x451E8..0x452AC: each base's type byte in the grid+1620 array, computed from its own
     * product columns and looked up by the base's bUnk4 type byte. */
    for (i32 b = 0; b < nBaseCount; ++b) {
        BaseRec *pBase = (pEnv->apBases != NULL) ? pEnv->apBases[b] : NULL;
        if (pBase == NULL) {
            continue;
        }
        const u16 *pC = (const u16 *)(pEnv->pProductBlock + 12800) + b * PUFO_PRODUCT_ENTRIES_PER_BASE;
        const i32 *pB = (const i32 *)(pEnv->pProductBlock + 6400)  + b * PUFO_PRODUCT_ENTRIES_PER_BASE;
        i32 nValue = (-(i32)pC[PUFO_PRODUCT_TYPE_COL_A]) & 0xFF; /* 0x45230: RSB then AND #0xFF */
        nValue -= (i32)(u8)pC[PUFO_PRODUCT_TYPE_COL_B];          /* 0x45244: LDRB of the u16's low byte */
        nValue -= (i32)(u8)pC[PUFO_PRODUCT_TYPE_COL_C];          /* 0x45258 */
        nValue += (u8)pB[PUFO_PRODUCT_TYPE_COL_C];               /* 0x4526C */
        nValue += (u8)pB[PUFO_PRODUCT_TYPE_COL_B];               /* 0x45280 */
        nValue += (u8)pB[PUFO_PRODUCT_TYPE_COL_A];               /* 0x45294 */
        (void)BaseGrid_SetTypeByte(pEnv->apBases, nBaseCount, pEnv->pBases->abUnk159DBC,
                                   pBase->bUnk4, (i8)nValue);
    }

    /* 0x452BC..0x4535C: the facType fill. For every cell whose tileA byte is 0 AND whose
     * facType byte is 0, facType becomes 255. The ARM's 6*(24*b + row + 18) + k + 372 is
     * fac[b] + 108 + 6*row + k — the facType layer of the same 144-byte block. */
    for (i32 b = 0; b < nBaseCount; ++b) {
        u8 *pFac = pEnv->abFacilityGrid + 372 + 144 * b;
        for (i32 k = 0; k < 6; ++k) {
            for (i32 row = 0; row < 6; ++row) {
                if (pFac[6 * row + k] == 0 && pFac[108 + 6 * row + k] == 0) {
                    pFac[108 + 6 * row + k] = 0xFF; /* 0x45324 */
                }
            }
        }
    }

    /* 0x45388..0x45588: an old save re-opens the base-facility flags for every unlocked
     * research topic, through the jumptable's permuted topic -> flag map. */
    if (nVer < 3 && *pEnv->puResearchEntries != 0) {
        i32 nTopics = (i32)*pEnv->puResearchEntries;
        for (i32 topic = 0; topic < nTopics && topic < 17; ++topic) {
            i8 nFlag = s_abResearchUnlockFlag[topic];
            if (nFlag >= 0 && pEnv->pResearch->aTopics[topic].abUnk10[3] != 0) { /* LDRB [R2,#0xD] */
                pEnv->pBases->abFlag20[nFlag] = 1;
            }
        }
    }

    /* 0x45590..0x45734: an old save rebuilds the product arrays through the facility->item
     * map. The four arrays go to scratch, get zeroed, and each facility cell (144 bytes per
     * base at block+16000, one byte per topic, the byte is the item index) re-spreads the
     * scratch entry [200*base + topic] into [item + 200*base]. */
    if (nVer < 3) {
        static u8  aScratchA[6400];
        static u8  aScratchB[6400];
        static u8  aScratchC[3200];
        static u8  aScratchD[3200];
        u8     *pA   = pEnv->pProductBlock;
        u8     *pB   = pEnv->pProductBlock + 6400;
        u8     *pC   = pEnv->pProductBlock + 12800;
        u8     *pMap = pEnv->pProductBlock + 16000;
        u16    *pD   = pEnv->apManufCols; /* auManufCol @ 0x15E19C. The original reaches it as anchor+0x4304,
                                           * the array sitting right after the product block; the shell keeps
                                           * the two apart, so the copy/clear/re-spread must go through the env
                                           * pointer. Reaching it as block+17152 wrote 3200 bytes past the end of
                                           * the block and cleared the world record's tail (its own frame-hook
                                           * pointers included). */
        i32     nTopics = (i32)*pEnv->puResearchEntries;

        memcpy(aScratchA, pA, sizeof(aScratchA)); /* 0x455A4 */
        memcpy(aScratchB, pB, sizeof(aScratchB)); /* 0x455C0 */
        memcpy(aScratchC, pC, sizeof(aScratchC)); /* 0x455DC */
        memcpy(aScratchD, pD, sizeof(aScratchD)); /* 0x455F8 */
        memset(pA, 0, 6400);                      /* 0x45608 */
        memset(pB, 0, 6400);                      /* 0x45618 */
        memset(pC, 0, 3200);                      /* 0x45628 */
        memset(pD, 0, 3200);                      /* 0x45638 */

        for (i32 b = 0; b < nBaseCount; ++b) {
            i32 nBaseOff = 200 * b;
            for (i32 col = 0; col < nTopics; ++col) {
                u8 nItem = pMap[144 * b + col]; /* 0x45688 */
                if (nItem == 0) {
                    continue; /* 0x45690: an empty cell carries nothing */
                }
                ((i32 *)pA)[nItem + nBaseOff] = ((const i32 *)aScratchA)[nBaseOff + col]; /* 0x456AC */
                ((i32 *)pB)[nItem + nBaseOff] = ((const i32 *)aScratchB)[nBaseOff + col]; /* 0x456C8 */
                ((u16 *)pC)[nItem + nBaseOff] = ((const u16 *)aScratchC)[nBaseOff + col]; /* 0x456F0 */
                pD[nItem + nBaseOff] = ((const u16 *)aScratchD)[nBaseOff + col]; /* 0x45714, STRH */
            }
        }
    }

    /* 0x45740..0x45794: the shot object is apObjects[0] (the load at 0x45750 reads
     * [Map+0x108], i.e. the object table's first entry — there is no separate global).
     * nCraftCount words starting at word 29 (byte 58 = aItemQty[0]) are zeroed, then word
     * 101 (byte 0xCA) is zeroed even when the craft count is zero — the loop gate is a
     * BLS that jumps straight to the 0xCA store. */
    if (pEnv->puShotObjectWords != NULL) {
        for (i32 i = 0; i < nCraftCount; ++i) {
            pEnv->puShotObjectWords[29 + i] = 0; /* 0x45764: STRH R7,[0x3A + apObjects[0] + 2*i] */
        }
        pEnv->puShotObjectWords[101] = 0; /* 0x4578C: STRH R7,[apObjects[0],#0xCA] */
    }

    /* 0x4579C..0x457F0: detection mirrors visibility after a load. */
    for (i32 i = 0; i < PtrArray_GetCount(pEnv->pStrategList); ++i) {
        StrategItem *pItem = PtrArray_Get(pEnv->pStrategList, i);
        if (pItem != NULL && pItem->nType == 1 && pItem->bActive == 1) {
            pItem->bDetected = pItem->bVisible; /* 0x457F0 */
        }
    }

    /* 0x4580C..0x458B8: per base, the radar byte and the five needed-flag passes with the
     * budgets the original computes internally. */
    for (i32 b = 0; b < nBaseCount; ++b) {
        pEnv->pWorld->apRadarPct[b] = Base_CountEmptyFacilityCells(pEnv->abFacilityGrid, b); /* 0x4582C: a DWORD store */
    }
    for (i32 b = 0; b < nBaseCount; ++b) {
        i32 nSpace  = Base_CalcOccupiedSpace(pEnv->apSoldiers, *pEnv->pnSoldierCount,
                                             pEnv->apObjects, pEnv->nObjectCount, b);
        i32 nStores = Base_CalcStorageUsed(pEnv->apObjects, pEnv->nObjectCount, b);
        (void)Base_UpdateLivingQuartersNeeded(pEnv->abFacilityGrid, b,
            nSpace + Base_CountOutboundTransferSpace(pEnv->pSmap->aSites, nSiteCount, -1,
                                                     pEnv->apBases, b, true)); /* 0x45888 */
        (void)Base_UpdateStoresNeeded(pEnv->abFacilityGrid, b,
            nStores + Base_CountItemQtyAcrossBases(pEnv->pSmap->aSites, nSiteCount, -1,
                                                   pEnv->apBases, b,
                                                   pEnv->apObjects, pEnv->nObjectCount,
                                                   true)); /* 0x45894: the -1 gate is zero, see the helper */
        (void)Base_UpdatePsiLabNeeded(pEnv->abFacilityGrid, b,
            Base_CountPsiSoldiers(pEnv->apSoldiers, *pEnv->pnSoldierCount, b)); /* 0x458A0 */
        (void)Base_UpdateLabNeeded(pEnv->abFacilityGrid, b,
            Base_CountActiveResearch(pEnv->apResearchCols + 144 * b, (i32)*pEnv->puResearchEntries)); /* 0x458AC */
        (void)Base_UpdateWorkshopNeeded(pEnv->abFacilityGrid, b,
            Base_CountActiveManuf(pEnv->apManufCols + 200 * b, PUFO_PRODUCT_ENTRIES_PER_BASE)); /* 0x458B8 */
    }

    *pEnv->pbCraftSelectAssaultFlag = 0;          /* 0x458DC */
    pEnv->pStrateg->bSaveVersion = 3;             /* 0x458E4: the loaded world is stamped current */
    (void)bOk; /* deviation: the original ignores every read failure after the existence test */
    return 1; /* 0x458F8 */
}
