/* pufo_rmp.h - route/spawn nodes (.rmp).
 *
 * Evidence: Map_LoadRmp @ 0x55060, Map_FreeRmpNodes @ 0x55018,
 *           Map_PickRmpSpawnForUnit @ 0x55418.
 *
 * ## .rmp layout
 *
 * Disk records are 24 (0x18) bytes. Node count = `size / 0x18`, which TRUNCATES.
 * Confirmed against the shipped main.dat: 156 of 158 members are exact multiples
 * of 24, but UFO_000.RMP (140 bytes) and UFO_010.RMP (520 bytes) are not, so the
 * original silently drops 20 and 16 trailing bytes respectively. Reproduced as
 * integer division so the node counts match.
 *
 * Per record, byte offsets as read by Map_LoadRmp:
 *
 *     0   local Y      node Y = 10 * tileY + b[0]
 *     1   local X      node X = 10 * tileX + b[1]
 *     2   height       node Z = level - b[2] - 1
 *     4   link 0 target, 5-6  link 0 payload
 *     7   link 1 target, 8-9  link 1 payload
 *     10  link 2 target, 11-12
 *     13  link 3 target, 14-15
 *     16  link 4 target, 17-18
 *     19  bFlag19
 *     20  bFlag20
 *     21-23 unused
 *
 * So there are FIVE links of 3 bytes each at offsets 4..18, not ten. That is the
 * one place the on-disk and in-memory shapes differ: the in-memory RmpNode has a
 * 10-word aLink array and a 28-byte size, while the disk record is 24 bytes.
 *
 * Link sentinels, from the load loop:
 *     251 -> -5   (north edge, node has no Y-1 neighbour)
 *     252 -> -4   (south edge)
 *     253 -> -3   (east edge)
 *     254 -> -2   (west edge)
 *     255 -> -1   (no link)
 *     other    -> target - runRecordIndex + globalNodeIndex, i.e. the record's own index inside this .rmp
 *                 rebased onto the list the earlier modules already filled: target + nodesBeforeThisModule
 *                 (0x552EC: SUB R2,R2,LR with LR the run's record index, ADD R2,R2,R1 with R1 the ALREADY
 *                 POSTED nRmpCount, then SUB R2,R2,#1 -- the run index cancels)
 *
 * The payload pair (offsets 5 and 6) is stored into the low and high bytes of the
 * next word, i.e. a little-endian u16 in aLink[linkIndex + 1].
 */
#ifndef PUFO_RMP_H
#define PUFO_RMP_H

#include "game/pufo_mapcell.h"
#include "game/pufo_unit.h"
#include "pufo_common.h"

/* On-disk record size. */
#define PUFO_RMP_REC_SIZE 0x18

/* Links per node. */
#define PUFO_RMP_LINKS 5

/* In-memory node, 28 bytes in the original (RmpNode @ ordinal 110). */
typedef struct RmpNode {
    u16  nIndex;     /* 0x00 */
    u8   nY;         /* 0x02 */
    u8   nX;         /* 0x03 */
    u8   nZ;         /* 0x04 */
    u8   gap_5;      /* 0x05 */
    u16  aLink[10];  /* 0x06: 10 words = 5 links x (target + payload u16) */
    u8   bFlag19;    /* 0x1a */
    u8   bFlag20;    /* 0x1b */
} RmpNode;

_Static_assert(sizeof(RmpNode) == 28, "RmpNode must stay 28 bytes");

/* Link sentinels. A direction sentinel whose edge guard trips stores NONE: the load pre-writes -1 into the
 * target word (0x55294) and the guard path jumps over every other write in that iteration. */
#define PUFO_RMP_LINK_NONE  ((i16)-1)
#define PUFO_RMP_LINK_EDGE_N ((i16)-5)
#define PUFO_RMP_LINK_EDGE_S ((i16)-4)
#define PUFO_RMP_LINK_EDGE_E ((i16)-3)
#define PUFO_RMP_LINK_EDGE_W ((i16)-2)

/* The node collection plus the module-placement records, mirroring the
 * original's TactBuf11408 (11408 bytes). Only the parts this module owns are
 * modelled; the module-placement tail is declared for completeness. */
#define PUFO_RMP_MAX_NODES 1500
#define PUFO_RMP_MAX_MODULES 60

typedef struct RmpData {
    RmpNode *apNodes[PUFO_RMP_MAX_NODES]; /* 0x0000 */
    u16      nNodeCount;                  /* 0x1770 */
    i32      nModCount;                   /* 0x232c */
    i32      anModTileX[PUFO_RMP_MAX_MODULES];   /* 0x2330 */
    i32      anModTileY[PUFO_RMP_MAX_MODULES];   /* 0x2420 */
    i32      anModSizeX[PUFO_RMP_MAX_MODULES];   /* 0x2510 */
    i32      anModSizeY[PUFO_RMP_MAX_MODULES];   /* 0x2600 */
    char     aszModName[PUFO_RMP_MAX_MODULES][20]; /* 0x26f0 */
    i32      anModLevel[PUFO_RMP_MAX_MODULES];   /* 0x2ba0 */
} RmpData;

void Rmp_Init(RmpData *pRmp);

/* TactBuf11408_ctor @ 0x54ff8. Clears only the u16 at +6000 and the dword at +9004
 * (nModCount); it is NOT a full clear, so the node array and node count survive. */
void TactBuf11408_ctor(RmpData *pBuf);

/* Map_FreeRmpNodes @ 0x55018. Does NOT clear nNodeCount, exactly like the
 * original; call Rmp_Init to do that. Freeing twice without a reset would be a
 * double free, so the pointers are nulled here (a deviation, see the notes). */
void Rmp_FreeNodes(RmpData *pRmp);

/* Map_LoadRmp @ 0x55060.
 *
 * Reads "<pszBase>.rmp" and converts it to nodes. nTileX/nTileY/nSizeX/nSizeY/
 * nLevel describe where the module sits in the map, so each node's world
 * coordinates can be derived. nMapDimX/nMapDimY are the map's tile dimensions;
 * the original reads them from the context at +86299/+86300 and uses them only
 * for the link edge tests.
 *
 * Returns the number of nodes parsed. */
i32 Rmp_Load(RmpData *pRmp, const char *pszBase,
             i32 nTileX, i32 nTileY, i32 nSizeX, i32 nSizeY, i32 nLevel,
             i32 nMapDimX, i32 nMapDimY);

/* Map_PickRmpSpawnForUnit @ 0x55418.
 *
 * Walks the spawn list from a random start, skipping nodes the unit cannot use,
 * and places the unit on the first acceptable free node. Returns the node index
 * used, or -1 when no node was acceptable.
 *
 * The filters, in the original's order:
 *   - unit type number (pUnit[0xA0]): types 0..8 require the node's bFlag20 == 1; types above 8 compare
 *     bFlag20 against the unit's movement class (pUnit[0xE2], with 8 normalised to 4). This is the type
 *     number itself, not the large-unit footprint flag: types 4..8 are large but still take the first path.
 *   - when the movement class is 2, bFlag20 must be 2, or 0 for a unit whose pUnit[0x1B3] byte is clear
 *     and whose mission is not an XCom base defence
 *   - bFlag19 gates the unit's two type flags from byte_A7B80
 *   - the destination cell must be free
 *
 * pUnit is passed as bytes because only three fields are read and the full
 * Soldier tail is not modelled; see Rmp_NodeAcceptsUnit for the exact offsets. */
typedef struct RmpUnitInfo {
    u8 nUnitType;    /* pUnit[0xA0] */
    u8 nMoveClass;   /* pUnit[0xE2], normalised at the point of use: 8 -> 4 */
    u8 bFlag0;       /* pUnit[0x1B3] (435), the "kneeling/primed" byte */
    /* byte_A7B80[4 * nUnitType] and byte_A7B80[4 * nUnitType + 1] */
    u8 bTypeFlagA;
    u8 bTypeFlagB;
} RmpUnitInfo;

/* Fills pInfo from a Soldier. Separate so callers can also build it by hand. */
void Rmp_UnitInfoFromSoldier(RmpUnitInfo *pInfo, const Soldier *pUnit);

/* The per-node acceptance test, split out so it is testable on its own.
 * bXcomBaseMission is `nMissionType == MissionType_XcomBase`, which one branch
 * of the original consults. */
bool Rmp_NodeAcceptsUnit(const RmpNode *pNode, const RmpUnitInfo *pInfo,
                         bool bXcomBaseMission);

/* Picks a spawn node and places the unit. pfnLookup resolves the destination
 * cell. Returns the node index used, or -1 when nothing was acceptable. */
i32 Rmp_PickSpawnForUnit(RmpData *pRmp, Soldier *pUnit,
                         MapCellLookup pfnLookup, void *pUser,
                         bool bXcomBaseMission);

#endif /* PUFO_RMP_H */
