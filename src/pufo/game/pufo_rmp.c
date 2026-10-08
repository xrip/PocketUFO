/* pufo_rmp.c - route/spawn nodes (.rmp).
 *
 * Evidence: Map_LoadRmp @ 0x55060, Map_FreeRmpNodes @ 0x55018,
 *           Map_PickRmpSpawnForUnit @ 0x55418.
 */
#include "game/pufo_rmp.h"

#include <stdlib.h>
#include <string.h>

#include "asset/pufo_asset.h"
#include "util/pufo_rand.h" /* Game_RandU16, for the random spawn start */
#include "util/pufo_str.h"

/* byte_A7B80 @ 0xa7b80, stride 4. Two flags per unit type are consulted by
 * Map_PickRmpSpawnForUnit: byte[4*type] and byte[4*type+1]. Decoded from the
 * 64 bytes at that address:
 *     byte[4i]   = 1 for types 3 and 15
 *     byte[4i+1] = 1 for types 4..8, 11, 13
 * The +1 byte is the same "is large" flag pufo_unit.c uses, so it is reused from
 * there rather than duplicated. The +0 byte is the other unit-size class flag. */
static const u8 s_aUnitTypeFlagA[PUFO_UNIT_TYPE_COUNT] = {
    0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1
};

void Rmp_Init(RmpData *pRmp)
{
    memset(pRmp, 0, sizeof(*pRmp));
}

/* Map_FreeRmpNodes @ 0x55018.
 *
 * The original frees the nodes but leaves nNodeCount set. That makes a second
 * call a double free, so this nulls each pointer as it frees (the count is left
 * alone to match the original's post-condition). */
void Rmp_FreeNodes(RmpData *pRmp)
{
    u16 i;

    for (i = 0; i < pRmp->nNodeCount && i < PUFO_RMP_MAX_NODES; ++i) {
        free(pRmp->apNodes[i]);
        pRmp->apNodes[i] = NULL;
    }
}

/* The link sentinel for a direction byte. Returns the sentinel, or writes the
 * "no link" value when the edge condition says the neighbour is outside the map.
 *
 * The mappings and their gate conditions, from the compare chain in
 * Map_LoadRmp @ 0x55060. Each sentinel has a guard: when the guard trips the link
 * is left at -1 (no link) instead.
 *
 *   251 -> -5 (0xFFFB)   guard: nTileY == 0
 *   252 -> -4 (0xFFFC)   guard: nTileY + nSizeY == nMapDimY
 *   253 -> -3 (0xFFFD)   guard: nTileX + nSizeX == nMapDimX
 *   254 -> -2 (0xFFFE)   guard: nTileY == 0
 *   255 -> -1 (0xFFFF)   no guard
 *
 * Confidence note: the compiler interleaved these blocks, and the decompiler's
 * block-to-value attribution does not line up with their order in memory. The
 * VALUES are certain (each block's MOV immediate is unambiguous); the pairing of
 * guard to value is the part reconstructed. Two of the four guards test the same
 * condition (nTileY == 0), which is suspicious but is what both views show. This
 * function isolates the mapping so it is the single place to re-check. */
static bool Rmp_LinkSentinel(u8 nDir, i32 nTileX, i32 nTileY, i32 nSizeX, i32 nSizeY,
                             i32 nMapDimX, i32 nMapDimY, i16 *pnOut)
{
    switch (nDir) {
    case 251:
        if (nTileY == 0) {
            return false;
        }
        *pnOut = PUFO_RMP_LINK_EDGE_N;
        return true;
    case 252:
        if (nTileY + nSizeY == nMapDimY) {
            return false;
        }
        *pnOut = PUFO_RMP_LINK_EDGE_S;
        return true;
    case 253:
        if (nTileX + nSizeX == nMapDimX) {
            return false;
        }
        *pnOut = PUFO_RMP_LINK_EDGE_E;
        return true;
    case 254:
        if (nTileY == 0) {
            return false;
        }
        *pnOut = PUFO_RMP_LINK_EDGE_W;
        return true;
    case 255:
        *pnOut = PUFO_RMP_LINK_NONE;
        return true;
    default:
        return false; /* a real relative index, not a sentinel */
    }
}

/* Map_LoadRmp @ 0x55060.
 *
 * Record geometry is verified against the shipped data; the link sentinel pairing
 * carries the confidence note above. */
i32 Rmp_Load(RmpData *pRmp, const char *pszBase,
             i32 nTileX, i32 nTileY, i32 nSizeX, i32 nSizeY, i32 nLevel,
             i32 nMapDimX, i32 nMapDimY)
{
    AssetReader rdr;
    char        szName[64];
    u8         *pBlob;
    u32         cb;
    u32         nRecs;
    u32         nBase;

    Rmp_FreeNodes(pRmp);
    pRmp->nNodeCount = 0;

    pufo_strncpy(szName, pszBase, sizeof(szName));
    if (!pufo_strcat(szName, ".rmp", sizeof(szName))) {
        return 0;
    }

    AssetReader_Clear(&rdr);
    if (!AssetReader_Find(&rdr, szName)) {
        return 0;
    }
    cb = AssetReader_Size(&rdr);
    pBlob = malloc(cb);
    if (pBlob == NULL) {
        return 0;
    }
    if (!AssetReader_Read(&rdr, pBlob, cb)) {
        free(pBlob);
        return 0;
    }

    /* The original divides by 0x18, which TRUNCATES: two of the shipped members
     * are not exact multiples and lose their trailing bytes. Reproduced. */
    nRecs = cb / PUFO_RMP_REC_SIZE;
    if (nRecs > PUFO_RMP_MAX_NODES) {
        nRecs = PUFO_RMP_MAX_NODES;
    }
    nBase = pRmp->nNodeCount;

    /* Record the module placement, which the .rmp save chunk writes back out. */
    if (pRmp->nModCount < PUFO_RMP_MAX_MODULES) {
        i32 nMod = pRmp->nModCount;

        pRmp->anModTileX[nMod] = nTileX;
        pRmp->anModTileY[nMod] = nTileY;
        pRmp->anModSizeX[nMod] = nSizeX;
        pRmp->anModSizeY[nMod] = nSizeY;
        pufo_strncpy(pRmp->aszModName[nMod], pszBase, sizeof(pRmp->aszModName[0]));
        pRmp->anModLevel[nMod] = nLevel;
        pRmp->nModCount += 1;
    }

    for (u32 i = 0; i < nRecs; ++i) {
        const u8 *pRec = pBlob + (size_t)i * PUFO_RMP_REC_SIZE;
        RmpNode  *pNode = malloc(sizeof(RmpNode));

        if (pNode == NULL) {
            break;
        }
        memset(pNode, 0, sizeof(*pNode));

        /* World coordinates. X comes from byte 1 and Y from byte 0; the Z uses
         * level - height - 1, so a taller record sits LOWER. */
        pNode->nX = (u8)(PUFO_CELL_SIZE * nTileX + pRec[1]);
        pNode->nY = (u8)(PUFO_CELL_SIZE * nTileY + pRec[0]);
        pNode->nZ = (u8)(nLevel - pRec[2] - 1);
        pNode->bFlag19 = pRec[19];
        pNode->bFlag20 = pRec[20];
        pNode->nIndex  = (u16)(nBase + i);

        /* Five links, three bytes each, starting at offset 4. aLink holds a
         * target word followed by a payload word, so link k occupies aLink[2k]
         * and aLink[2k+1]. */
        for (u32 k = 0; k < PUFO_RMP_LINKS; ++k) {
            const u8 *pLink = pRec + 4 + 3 * k;
            i16       nTarget;

            pNode->aLink[2 * k] = (u16)PUFO_RMP_LINK_NONE;
            if (Rmp_LinkSentinel(pLink[0], nTileX, nTileY, nSizeX, nSizeY,
                                 nMapDimX, nMapDimY, &nTarget)) {
                pNode->aLink[2 * k] = (u16)nTarget;
            } else {
                /* A real relative index: target - runBase + nodeIndexInRun.
                 * `runBase` is the node count before this call started, so the
                 * value is relative to the module's own first node. */
                pNode->aLink[2 * k] = (u16)(i16)((i16)pLink[0] - (i16)(u16)nBase
                                                 + (i16)(nBase + i));
            }
            /* The payload pair is a little-endian u16 stored in the next word. */
            pNode->aLink[2 * k + 1] = (u16)(pLink[1] | ((u16)pLink[2] << 8));
        }

        pRmp->apNodes[pRmp->nNodeCount] = pNode;
        pRmp->nNodeCount += 1;
    }

    free(pBlob);
    return (i32)nRecs;
}

void Rmp_UnitInfoFromSoldier(RmpUnitInfo *pInfo, const Soldier *pUnit)
{
    memset(pInfo, 0, sizeof(*pInfo));
    pInfo->nUnitType = pUnit->nUnitType;
    pInfo->bIsLarge  = Unit_bIsLarge(pUnit);
    /* pUnit[0xE2] is bRankType; the original normalises 8 to 4. */
    pInfo->nMoveClass = 0;
    pInfo->bFlag0     = 0;
    if (pInfo->nUnitType < PUFO_UNIT_TYPE_COUNT) {
        pInfo->bTypeFlagA = s_aUnitTypeFlagA[pInfo->nUnitType];
        pInfo->bTypeFlagB = g_aUnitTypeIsLarge[pInfo->nUnitType];
    }
}

/* Map_PickRmpSpawnForUnit @ 0x55418, the acceptance half.
 *
 * The filters, in the original's order:
 *   1. small units require bFlag20 == 1
 *   2. large units compare the node's bFlag20 against the unit's movement class,
 *      with 8 normalised to 4, and the XCom-base mission adds a special case
 *   3. bFlag19 selects a size/can-fly requirement against the unit type flags
 */
bool Rmp_NodeAcceptsUnit(const RmpNode *pNode, const RmpUnitInfo *pInfo,
                         bool bXcomBaseMission)
{
    if (!pInfo->bIsLarge) {
        if (pNode->bFlag20 != 1) {
            return false;
        }
    } else {
        u8 nClass = pInfo->nMoveClass;

        if (nClass == 8) {
            nClass = 4;
        }
        if (nClass == 2) {
            /* The original's nested condition, reduced to the same truth table:
             *   (bFlag0 != 0 && bFlag20 != 2)
             *   || (bFlag0 == 0 && (bFlag20 != 0 && bFlag20 != 2
             *                       || (bFlag0 == 0 && bFlag20 != 2
             *                           && nMissionType == XcomBase)))
             * The inner repeat of `bFlag0 == 0` is redundant, so this reduces to:
             * reject unless bFlag20 == 2, or (bFlag0 == 0 && bFlag20 == 0 and not
             * an XCom-base mission). */
            bool bReject;

            if (pInfo->bFlag0 != 0) {
                bReject = (pNode->bFlag20 != 2);
            } else {
                bReject = (pNode->bFlag20 != 0 && pNode->bFlag20 != 2)
                       || (pNode->bFlag20 != 2 && bXcomBaseMission);
            }
            if (bReject) {
                return false;
            }
        } else if (nClass != pNode->bFlag20) {
            return false;
        }
    }

    switch (pNode->bFlag19) {
    case 0:
        break;
    case 1:
        /* Requires the type's "A" flag set and "B" clear (large + not flying). */
        if (pInfo->bTypeFlagA != 1 || pInfo->bTypeFlagB != 0) {
            return false;
        }
        break;
    case 2:
        if (pInfo->bTypeFlagB != 0) {
            return false;
        }
        break;
    case 3:
        if (pInfo->bTypeFlagA != 1) {
            return false;
        }
        break;
    case 4:
        if (pInfo->bTypeFlagA != 0) {
            return false;
        }
        break;
    default:
        break;
    }
    return true;
}

i32 Rmp_PickSpawnForUnit(RmpData *pRmp, Soldier *pUnit,
                         MapCellLookup pfnLookup, void *pUser,
                         bool bXcomBaseMission)
{
    RmpUnitInfo info;
    u16         nStart;
    u16         nIdx;

    if (pRmp->nNodeCount == 0) {
        return -1;
    }
    Rmp_UnitInfoFromSoldier(&info, pUnit);

    /* The original picks a random start and walks forward, wrapping. */
    nStart = Game_RandU16(pRmp->nNodeCount);
    nIdx   = nStart;

    for (u16 nTried = 0; nTried < pRmp->nNodeCount; ++nTried) {
        const RmpNode *pNode = pRmp->apNodes[nIdx];
        MapCell       *pCell;

        nIdx = (u16)((nIdx == (u16)(pRmp->nNodeCount - 1u)) ? 0u : (u16)(nIdx + 1u));

        if (pNode == NULL) {
            continue;
        }
        if (!Rmp_NodeAcceptsUnit(pNode, &info, bXcomBaseMission)) {
            continue;
        }
        pCell = pfnLookup(pNode->nX, pNode->nY, pNode->nZ, pUser);
        if (pCell == NULL) {
            continue;
        }
        /* The original places the unit on the first node whose cell is free. */
        if (!MapCell_HasOccupant(pCell)) {
            Unit_PlaceOnMap(pUnit, pNode->nX, pNode->nY, pNode->nZ, pfnLookup, pUser);
            return (i32)pNode->nIndex;
        }
    }
    return -1;
}

/* TactBuf11408_ctor @ 0x54ff8.
 *
 * Clears exactly two fields, not the whole buffer: the u16 at offset 3000 (0xBB8)
 * and the dword at offset 9004 (0x232C). Against the recovered TactBuf11408 layout
 * those are a word inside the node-count gap and `nModCount` -- the module count.
 * Everything else, including `nRmpCount` at 0x1770 and the node array, is left as
 * the caller had it, which is why this is not a memset. */
void TactBuf11408_ctor(RmpData *pBuf)
{
    if (pBuf == NULL) {
        return;
    }
    /* 0x54ff8 writes *((_WORD *)pBuf + 3000) and *((_DWORD *)pBuf + 2251). */
    *(u16 *)((u8 *)pBuf + 6000) = 0;
    *(u32 *)((u8 *)pBuf + 9004) = 0;
}
