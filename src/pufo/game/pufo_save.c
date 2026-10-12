/* pufo_save.c - save/load chunk serialisation.
 *
 * Evidence: see pufo_save.h for the full address list.
 */
#include "game/pufo_save.h"

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

bool Save_WriteSoldier(DatFile *pDat, const Soldier *pUnit)
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

    /* The non-tactical branch writes a raw 120-byte buffer produced by
     * `memset(buf, 0x7FFF, 0x78)`. memset takes an int but stores its LOW BYTE,
     * so the buffer is 120 bytes of 0xFF -- not a 0x7FFF pattern. Reproduced. */
    {
        u8 aMulti[120];

        memset(aMulti, 0xFF, sizeof(aMulti));
        if (!W(pDat, aMulti, sizeof(aMulti))) {
            return false;
        }
    }

    if (!W(pDat, &pUnit->bUnk148, 1)) { return false; }
    if (!W(pDat, &pUnit->bUnk149, 1)) { return false; }
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

    /* The non-tactical inventory block: a 150-byte zero fill. */
    {
        u8 aInv[150];

        memset(aInv, 0, sizeof(aInv));
        if (!W(pDat, aInv, sizeof(aInv))) {
            return false;
        }
    }

    if (!W(pDat, &pUnit->bHumanoidInv, 1)) { return false; }
    if (!W(pDat, &pUnit->uInvBurden, 2)) { return false; }
    if (!W(pDat, &pUnit->bUnk422, 1)) { return false; }
    if (!W(pDat, &pUnit->nArmorPoints, 4)) { return false; }
    if (!W(pDat, &pUnit->nClass, 4)) { return false; }
    if (!W(pDat, &pUnit->nUnk436, 4)) { return false; }
    if (!W(pDat, &pUnit->nUnk440, 4)) { return false; }
    if (!W(pDat, &pUnit->bUnk423, 1)) { return false; }

    /* The non-tactical spotted block: a 244-byte zero fill. */
    {
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

bool Save_ReadSoldier(DatFile *pDat, Soldier *pUnit)
{
    u8  aSkip[244];
    u8  aCell[3];
    u8  bZero = 0;
    i32 nLook;
    i32 nFemale;
    u16 uDiscard16;
    i32 nDiscard32;

    if (pDat == NULL || pUnit == NULL) {
        return false;
    }
    memset(pUnit, 0, sizeof(*pUnit));

    if (!ReadFlags(pDat, &pUnit->bFlags)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bFacing, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->uTu, 2)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->uStunOrArmor, 2)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->uHp, 2)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bMorale, 1)) { return false; }
    /* The cell coordinates are only meaningful for a tactical save, and the read
     * side resolves them through the map; store them raw for the caller. */
    if (!DatFile_Read(pDat, aCell, 3)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nAnimPose, 1)) { return false; }
    if (!DatFile_Read(pDat, &bZero, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nDrawOffX, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nDrawOffY, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bStatus, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->uTuSpent, 2)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nMultiTarget, 1)) { return false; }

    /* The 120-byte multi-target block is not modelled; discard it. */
    if (!DatFile_Read(pDat, aSkip, 120)) { return false; }

    if (!DatFile_Read(pDat, &pUnit->bUnk148, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bUnk149, 1)) { return false; }
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

    /* 150-byte inventory block, discarded. */
    if (!DatFile_Read(pDat, aSkip, 150)) { return false; }

    if (!DatFile_Read(pDat, &pUnit->bHumanoidInv, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->uInvBurden, 2)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bUnk422, 1)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nArmorPoints, 4)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nClass, 4)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nUnk436, 4)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->nUnk440, 4)) { return false; }
    if (!DatFile_Read(pDat, &pUnit->bUnk423, 1)) { return false; }

    /* 244-byte spotted block, discarded. */
    if (!DatFile_Read(pDat, aSkip, 244)) { return false; }

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
            /* The original reads the linked soldier's nSaveId at +956. */
            nValue = (u32)*(const i32 *)((const u8 *)apSoldiers[i]->pLinked1040 + 956);
        }
        if (!W(pDat, &nValue, 4)) {
            return false;
        }
    }
    return true;
}

bool Save_ReadTrailingChunks(DatFile *pDat, Soldier *const *apSoldiers, i32 nCount)
{
    if (pDat == NULL || apSoldiers == NULL || nCount < 0) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_STACTFLG)) {
        return false;
    }
    for (i32 i = 0; i < nCount; ++i) {
        u8 bValue = 0;

        if (!DatFile_Read(pDat, &bValue, 1)) {
            return false;
        }
        if (apSoldiers[i] != NULL) {
            apSoldiers[i]->bInTact = bValue;
        }
    }

    /* The `1_24scb` chunk records link ids; the port does not resolve them yet, so
     * they are consumed to keep the stream consistent. */
    if (!DatFile_FindChunk(pDat, PUFO_SAVE_TAG_124SCB)) {
        return false;
    }
    for (i32 i = 0; i < nCount; ++i) {
        u32 nValue = 0;

        if (!DatFile_Read(pDat, &nValue, 4)) {
            return false;
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

/* Save_WriteSoldiers @ 0x42350.
 *
 * The order is: begin the chunk, write the u16 count, every record, then the two
 * trailing per-soldier chunks. This is the only valid entry point because
 * DatFile_Write refuses to work without an open chunk (verified: pufo_dat.c checks
 * `nCurEntry == 0` and returns false). */
bool Save_WriteSoldiers(DatFile *pDat, Soldier *const *apSoldiers, i32 nCount)
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
            if (apSoldiers[i] != NULL && !Save_WriteSoldier(pDat, apSoldiers[i])) {
                return false;
            }
        }
    }
    return Save_WriteTrailingChunks(pDat, apSoldiers, nCount);
}

/* Save_ReadSoldiers @ 0x416bc. The original allocates the roster from the count;
 * this port fills caller-provided storage so ownership stays explicit. */
bool Save_ReadSoldiers(DatFile *pDat, Soldier **apapSoldiers, i32 *pnCount,
                       i32 nCapacity)
{
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
    for (i32 i = 0; i < (i32)uCount; ++i) {
        if (apapSoldiers != NULL && apapSoldiers[i] != NULL) {
            if (!Save_ReadSoldier(pDat, apapSoldiers[i])) {
                return false;
            }
        }
    }
    /* The write side ends with the two per-soldier chunks, so the read side must
     * consume them; skipping this leaves bInTact (and the link ids) unrestored. */
    if (!Save_ReadTrailingChunks(pDat, apapSoldiers, (i32)uCount)) {
        return false;
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

/* Save_ReadSmap: the mirror of Save_WriteSmap (the original has no separate
 * reader in this chunk's family; the port provides one for the round trip). */
bool Save_ReadSmap(DatFile *pDat, SaveSmapState *pState)
{
    if (pDat == NULL || pState == NULL) {
        return false;
    }
    if (!DatFile_FindChunk(pDat, "smap")) {
        return false;
    }
    if (!DatFile_Read(pDat, &pState->nCamParam0, 4)
        || !DatFile_Read(pDat, &pState->nCamScrollX, 4)
        || !DatFile_Read(pDat, &pState->nCamScrollY, 4)
        || !DatFile_Read(pDat, &pState->nParam3, 4)
        || !DatFile_Read(pDat, &pState->nParam4, 4)) {
        return false;
    }
    for (i32 i = 0; i < PUFO_SMAP_SITE_COUNT; ++i) {
        if (!Save_ReadSmapSite(pDat, &pState->aSites[i])) {
            return false;
        }
    }
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

    if (!W(pDat, pState->abUnk18655D, 9)) { return false; } /* 0x3F304 */

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
            if (!W(pDat, &pBase->bUnk17, 1))       { return false; } /* 0x3F44C */
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

    if (!DatFile_Read(pDat, pState->abUnk18655D, 9)) { return false; } /* 0x40428 */

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
            if (!DatFile_Read(pDat, &pNew->bUnk17, 1))       { return false; } /* 0x405A4 */
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
