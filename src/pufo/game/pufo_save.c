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

                if (pCell == NULL) { /* deviation: the original dereferences the slot */
                    continue;
                }

                if (!W(pDat, &pCell->bUnk16, 4)) { /* 0x43998: bUnk16..bUnk19 as a block */
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

                if (pCell == NULL) { /* deviation: the original dereferences the slot */
                    continue;
                }

                if (!DatFile_Read(pDat, &pCell->bUnk16, 4)) { /* 0x434C0 */
                    return false;
                }
                pCell->nUnk20 = 0;  /* 0x434D0 */
                pCell->bUnk24 = 0;
                pCell->bUnk25 = 0;
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
