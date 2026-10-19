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

/* byte_A7B80 @ 0xa7b80, stride 4, 24 rows, read row by row out of the image:
 *
 *   type : 0      1      2      3      4      5      6      7
 *   row  : 0,0,1, 0 0,0,1,0 0,0,1,0 1,0,1,0 0,1,0,0 0,1,0,0 0,1,0,0 1,1,0,0
 *   type : 8      9      10     11     12     13     14     15
 *   row  : 1,1,0,0 0,0,1,0 0,0,1,0 1,0,1,0 0,0,1,0 1,0,1,0 0,0,0,1 0,0,0,1
 *   type : 16     17     18     19     20     21     22     23
 *   row  : 0,0,0,1 0,0,0,1 1,1,0,0 0,0,0,1 0,1,0,1 0,1,0,0 0,0,0,0 0,0,0,0
 *
 * So byte[4i] is 1 for types 3, 7, 8, 11, 13 and 18, and byte[4i+1] is 1 for types 4, 5, 6, 7, 8, 18, 20
 * and 21. Those are exactly g_aUnitTypeFlag0 and g_aUnitTypeIsLarge in pufo_unit.c, and Unit_bIsLarge
 * @ 0x49ecc confirms the second one: it is `byte_A7B80[4 * nUnitType + 1] == 1`. Both are reused from
 * there rather than duplicated here.
 *
 * The local table this replaces had byte[4i] set for types 3 and 15 only. Type 15 is 0,0,0,1 in the image,
 * so that slot was simply wrong, and types 7, 8, 11, 13 and 18 were missing. Every bFlag19 gate in
 * Map_PickRmpSpawnForUnit was therefore wrong for most unit types. */

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
/* Not a link value: the direction byte is a real record index. Every real link value the loader can store is
 * in -5..-1 or a node index, so this marker needs no counterpart in the node array. */
#define PUFO_RMP_DIR_NOT_SENTINEL 0x100

/* Returns the link word to store, or PUFO_RMP_DIR_NOT_SENTINEL when the direction byte is a real record
 * index. A guard that trips returns PUFO_RMP_LINK_NONE, exactly what the original leaves behind: 0x55294
 * pre-writes -1 into the target word and the guard path jumps to 0x553E8 without writing anything else, so
 * a tripped guard and the "no link" byte 255 end in the same stored value. */
static i32 Rmp_LinkSentinel(u8 nDir, i32 nTileX, i32 nTileY, i32 nSizeX, i32 nSizeY,
                            i32 nMapDimX, i32 nMapDimY)
{
    switch (nDir) {
    case 251:
        return (nTileY == 0) ? PUFO_RMP_LINK_NONE : PUFO_RMP_LINK_EDGE_N;
    case 252:
        return (nTileY + nSizeY == nMapDimY) ? PUFO_RMP_LINK_NONE : PUFO_RMP_LINK_EDGE_S;
    case 253:
        return (nTileX + nSizeX == nMapDimX) ? PUFO_RMP_LINK_NONE : PUFO_RMP_LINK_EDGE_E;
    case 254:
        return (nTileY == 0) ? PUFO_RMP_LINK_NONE : PUFO_RMP_LINK_EDGE_W;
    case 255:
        return PUFO_RMP_LINK_NONE;
    default:
        return PUFO_RMP_DIR_NOT_SENTINEL; /* a real record index, not a sentinel */
    }
}

/* Map_LoadRmp @ 0x55060.
 *
 * Record geometry is verified against the shipped data; the link sentinel pairing
 * carries the confidence note above.
 *
 * This function APPENDS and never frees. The node list is filled a module at a time: every call writes at
 * apRmpNodes[nRmpCount] and posts the count (0x5520C..0x5527C), so after a mission's terrain build the list
 * holds the nodes of EVERY module that was placed. Nothing here resets or releases anything. The count is
 * zeroed once per mission by TactBuf11408_ctor @ 0x54ff8 -- its only code caller is Tact_InitMission at
 * 0x3C630 -- and the only free in the whole beta is Map_FreeRmpNodes @ 0x55018, called once from
 * Tact_Shutdown at 0x3D6A0. An earlier version of the port freed the list and zeroed the count at the top of
 * this function, which left only the LAST module's nodes live: on the shell's base-defence mission that was
 * 8 nodes, all of them bFlag20 == 2, while a soldier (unit type 0) only accepts a bFlag20 == 1 node, so
 * Rmp_PickSpawnForUnit found no spawn for 7 of the 8 deployed units and they stayed parked on cell (1,1,0),
 * which is what the frame test's picture showed as a stacked squad. */
i32 Rmp_Load(RmpData *pRmp, const char *pszBase,
             i32 nTileX, i32 nTileY, i32 nSizeX, i32 nSizeY, i32 nLevel,
             i32 nMapDimX, i32 nMapDimY)
{
    AssetReader rdr;
    char        szName[64];
    u8         *pBlob;
    u32         cb;
    u32         nRecs;
    u32         nAppend;
    u32         nBase;

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
    nBase = pRmp->nNodeCount;

    /* The module placement is recorded only when the member has at least one record (0x550FC), and the
     * record is made before any node is appended. The original has no bound on nModCount at all -- a latent
     * fault -- so the port's capacity test here is a recorded deviation. */
    if (nRecs > 0 && pRmp->nModCount < PUFO_RMP_MAX_MODULES) {
        i32 nMod = pRmp->nModCount;

        pRmp->anModTileX[nMod] = nTileX;
        pRmp->anModTileY[nMod] = nTileY;
        pRmp->anModSizeX[nMod] = nSizeX;
        pRmp->anModSizeY[nMod] = nSizeY;
        pufo_strncpy(pRmp->aszModName[nMod], pszBase, sizeof(pRmp->aszModName[0]));
        pRmp->anModLevel[nMod] = nLevel;
        pRmp->nModCount += 1;
    }

    /* Appends run against the ACCUMULATED list, not against a per-module list. The original bounds nothing
     * here -- node 1500 would land on the count field itself at +0x1770 -- so capping at the array size is a
     * recorded deviation. The returned count is the record count the original returns, cap or no cap. */
    nAppend = nRecs;

    if (nBase + nAppend > PUFO_RMP_MAX_NODES) {
        nAppend = (nBase < PUFO_RMP_MAX_NODES) ? (PUFO_RMP_MAX_NODES - nBase) : 0u;
    }

    for (u32 i = 0; i < nAppend; ++i) {
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
            i32       nLink = Rmp_LinkSentinel(pLink[0], nTileX, nTileY, nSizeX, nSizeY,
                                               nMapDimX, nMapDimY);

            if (nLink == PUFO_RMP_DIR_NOT_SENTINEL) {
                /* 0x552EC: (pBuf[4] & 0xFF) - runRecordIndex + nRmpCount - 1, with nRmpCount already posted
                 * for this node, so the run index cancels and the stored word is the record's index inside
                 * this .rmp plus the node count the earlier modules left behind. An earlier version wrote
                 * pLink[0] + i, which agrees with the original only for a single-module load and only for the
                 * first record. No code in the beta reads these target words, so the correction changes no
                 * behaviour today; it is what a load of two or more modules has to get right. */
                pNode->aLink[2 * k] = (u16)(i16)((i16)pLink[0] + (i16)nBase);
            } else {
                /* A sentinel, or a sentinel whose edge guard tripped and leaves NONE behind. The payload pair
                 * is written on every one of those paths (0x553A4 sits at the join every store path falls
                 * through); only the guard-trip path skips it, jumping to 0x553E8 -- there the original's
                 * payload word keeps whatever operator new left. The port's node is memset, so its payload
                 * is 0. Recorded deviation; nothing in the beta reads a payload word of a link that has no
                 * target. */
                pNode->aLink[2 * k] = (u16)(i16)nLink;
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
    /* The original reads these two soldier bytes directly. An earlier version left both at zero, which
     * made the whole bRankType == 2 rule in Rmp_NodeAcceptsUnit unreachable. The 8 -> 4 normalisation is
     * done at the point of use, as the original does it, not here. */
    pInfo->nMoveClass = pUnit->bRankType;   /* pUnit[0x0E2] */
    pInfo->bFlag0     = pUnit->bUnk435;     /* pUnit[0x1B3] */
    if (pInfo->nUnitType < PUFO_UNIT_TYPE_COUNT) {
        pInfo->bTypeFlagA = g_aUnitTypeFlag0[pInfo->nUnitType];
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
    /* The original splits on the unit type number itself: `CMP R2, #8; BLS` at 0x55468, where R2 is
     * pUnit[0xA0]. It is NOT the large-unit flag. Types 4..8 are large by footprint but still take the
     * bFlag20 == 1 path, and types 9..17 are not large but take the movement-class path. */
    if (pInfo->nUnitType <= 8u) {
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
 * Clears exactly two fields, not the whole buffer: `*((_WORD *)pBuf + 3000)` is the u16 at BYTE offset
 * 6000 (0x1770), which is nNodeCount -- the field every Rmp_Load posts -- and `*((_DWORD *)pBuf + 2251)` is
 * the dword at 9004 (0x232C), which is nModCount. Those are the two counters the append loop in Map_LoadRmp
 * feeds, so this is the per-mission reset: the node array itself is left as the caller had it, which is why
 * this is not a memset and why the only release in the beta is Map_FreeRmpNodes from Tact_Shutdown. */
void TactBuf11408_ctor(RmpData *pBuf)
{
    if (pBuf == NULL) {
        return;
    }
    /* 0x54ff8 writes *((_WORD *)pBuf + 3000) and *((_DWORD *)pBuf + 2251). */
    *(u16 *)((u8 *)pBuf + 6000) = 0;
    *(u32 *)((u8 *)pBuf + 9004) = 0;
}
