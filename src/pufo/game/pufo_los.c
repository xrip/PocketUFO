/* pufo_los.c - voxel occupancy and line-of-sight tracing.
 *
 * Evidence: Tact_LoadLoftData @ 0x3bd6c, Map_TraceVoxelOccupancy @ 0x3242c,
 *           Map_TraceLos @ 0x30890.
 */
#include "game/pufo_los.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "asset/pufo_asset.h"
#include "util/pufo_rand.h"  /* Game_RandU16, used by the vertical-ray case */

/* One member of the pair: allocate, read, hand back the u16 count.
 *
 * The original sizes the block as `operator new(2 * (AssetReader_Size() / 2))` and then reads the
 * FULL member size into it. On an odd member that is a one-byte heap overflow. Both shipped members
 * are even (LOFTEMPS.DAT 3584, UNITLOFT.DAT 43008), so the two agree in practice; the port reads
 * only what it allocated, which is the deviation recorded in docs/parity/bugs.tsv. */
static bool Los_LoadRowTable(const char *pszName, const u16 **ppOut, u32 *pnOut)
{
    AssetReader rdr;
    u32         cb;
    u16        *pBuf;

    AssetReader_Clear(&rdr);
    if (!AssetReader_Find(&rdr, pszName)) {
        return false;
    }
    cb = 2u * (AssetReader_Size(&rdr) / 2u);
    if (cb == 0) {
        return false;
    }
    pBuf = (u16 *)malloc(cb);
    if (pBuf == NULL) {
        return false;
    }
    if (!AssetReader_Read(&rdr, pBuf, cb)) {
        free(pBuf);
        return false;
    }
    *ppOut = pBuf;
    *pnOut = cb / 2u;
    return true;
}

/* Tact_LoadLoftData @ 0x3bd6c.
 *
 * The original overwrites the two globals without freeing what the previous load left, so a second
 * call leaks the first pair; the caller owns the buffers and releases them (Tact_Shutdown's
 * appLoftemps / appUnitloft). Its return value is the UNITLOFT read result alone -- a failed
 * LOFTEMPS does not change it, and that is reproduced. */
bool Tact_LoadLoftData(LosTables *pTables)
{
    (void)Los_LoadRowTable("loftemps.dat", &pTables->pLoftemps, &pTables->nLoftemps);
    return Los_LoadRowTable("unitloft.dat", &pTables->pUnitloft, &pTables->nUnitloft);
}

/* word_A9488 @ 0xa9488, decoded from the 32 bytes at that address. It repeats
 * per unit type, giving the base sprite row for each facing with the kneeling
 * flag folded in: row = facing + 8 * kneeling. */
static const u8 s_aUnitRowBase[PUFO_UNITLOFT_ROWS_PER_TYPE] = {
    0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15
};

/* Map_TraceVoxelOccupancy reads sprite bytes at record offset (8 + z % 12) and
 * the movement-blocking byte at 0x2a. */

bool Los_TestRowBit(const u16 *pTable, u32 nTableLen, u32 nRowIndex, u32 nXBit)
{
    u32 nShift;

    /* The original indexes without a bound check. Returning false is the safe
     * behaviour and is recorded as a deviation. */
    if (pTable == NULL || nRowIndex >= nTableLen || nXBit > 15u) {
        return false;
    }
    /* Bit 15 is x = 0, so the shift counts down from 15. */
    nShift = 15u - nXBit;
    return ((pTable[nRowIndex] >> nShift) & 1u) != 0;
}

/* The terrain/unit sprite row index, used by both the wall/floor/ceiling tests
 * and the occupant test. */
static u32 Los_TerrainRow(const Mc1Rec *pSpr, u32 nZSub)
{
    const u8 *pBytes = (const u8 *)pSpr;

    return (u32)pBytes[PUFO_MC1_ROWTAB_TO_Z + (nZSub % PUFO_VOX_PER_CELL_Z)];
}

/* Map_TraceVoxelOccupancy @ 0x3242c.
 *
 * Order matters and is reproduced: wall A, wall B, floor, ceiling, then the
 * occupant. The first test that finds a set voxel bit decides the result.
 *
 * The wall tests carry an extra condition the floor/ceiling tests do not:
 *     blocks if bit && (sprite[0x2a] == 0 || !(cell->bFlags29 & 4))
 * so a wall with a non-zero block byte stops blocking once the cell's flag bit 2
 * is set -- that is how a destroyed or opened wall stops obstructing.
 *
 * The original's trailing "unit above" check is intentionally NOT reproduced: its
 * guard is `24 * nObjParam / 12 + z % 12 + 12 <= 11`, and since the first term is
 * `2 * nObjParam` the sum is at least 12 for any non-negative nObjParam, so the
 * body is unreachable under the original's own condition. */
LosHit Map_TraceVoxelOccupancy(const LosTables *pTables,
                               MapCellLookup pfnLookup, void *pUser,
                               u32 nX, u32 nY, u32 nZ,
                               const Soldier *pUnitSelExclude,
                               const Soldier *pUnitExtraExclude)
{
    MapCell *pCell;
    u32      nCellX = nX / PUFO_VOX_PER_CELL_X;
    u32      nCellY = nY / PUFO_VOX_PER_CELL_Y;
    u32      nLayer = nZ / PUFO_VOX_PER_CELL_Z;
    u32      nXBit  = nX % PUFO_VOX_PER_CELL_X;
    u32      nYSub  = nY % PUFO_VOX_PER_CELL_Y;
    u32      nZSub  = nZ % PUFO_VOX_PER_CELL_Z;

    pCell = pfnLookup((i32)nCellX, (i32)nCellY, (i32)nLayer, pUser);
    if (pCell == NULL) {
        return LOS_HIT_NONE;
    }
    if (pTables == NULL || pTables->pLoftemps == NULL) {
        return LOS_HIT_NONE;
    }

    /* Wall A. */
    if (pCell->pWallSprA != NULL) {
        const u8 *pBytes = (const u8 *)pCell->pWallSprA;
        u32       nRow   = nYSub + PUFO_VOX_PER_CELL_Y * Los_TerrainRow(pCell->pWallSprA, nZSub);

        if (Los_TestRowBit(pTables->pLoftemps, pTables->nLoftemps, nRow, nXBit)
            && (pBytes[PUFO_MC1_BLOCK_OFF] == 0 || (pCell->bFlags29 & 4u) == 0)) {
            return LOS_HIT_WALL_A;
        }
    }
    /* Wall B. */
    if (pCell->pWallSprB != NULL) {
        const u8 *pBytes = (const u8 *)pCell->pWallSprB;
        u32       nRow   = nYSub + PUFO_VOX_PER_CELL_Y * Los_TerrainRow(pCell->pWallSprB, nZSub);

        if (Los_TestRowBit(pTables->pLoftemps, pTables->nLoftemps, nRow, nXBit)
            && (pBytes[PUFO_MC1_BLOCK_OFF] == 0 || (pCell->bFlags29 & 4u) == 0)) {
            return LOS_HIT_WALL_B;
        }
    }
    /* Floor: no extra condition. */
    if (pCell->pFloorSpr != NULL) {
        u32 nRow = nYSub + PUFO_VOX_PER_CELL_Y * Los_TerrainRow(pCell->pFloorSpr, nZSub);

        if (Los_TestRowBit(pTables->pLoftemps, pTables->nLoftemps, nRow, nXBit)) {
            return LOS_HIT_FLOOR;
        }
    }
    /* Ceiling. */
    if (pCell->pCeilSpr != NULL) {
        u32 nRow = nYSub + PUFO_VOX_PER_CELL_Y * Los_TerrainRow(pCell->pCeilSpr, nZSub);

        if (Los_TestRowBit(pTables->pLoftemps, pTables->nLoftemps, nRow, nXBit)) {
            return LOS_HIT_CEILING;
        }
    }

    /* Occupant, using the unit silhouette table. */
    if (pTables->pUnitloft != NULL && MapCell_HasOccupant(pCell)) {
        const Soldier *pUnit = pCell->pUnit;

        if (pUnit != pUnitSelExclude && pUnit != pUnitExtraExclude) {
            u32 nRowBase;
            u32 nRow;

            if (Unit_bIsLarge(pUnit)) {
                /* The original derives a 0..3 sub-cell offset by checking which
                 * of the unit's four cells holds it at (x-1,y-1), (x,y-1) and
                 * (x+1,y+1). Reproduced with the same probe order. */
                u32 nSub = 1;
                MapCell *pNeighbour;

                pNeighbour = pfnLookup((i32)nCellX - 1, (i32)nCellY - 1, (i32)nLayer, pUser);
                if (pNeighbour != NULL && pNeighbour->pUnit == pUnit) {
                    nSub = 3;
                } else {
                    pNeighbour = pfnLookup((i32)nCellX, (i32)nCellY - 1, (i32)nLayer, pUser);
                    if (pNeighbour != NULL && pNeighbour->pUnit == pUnit) {
                        nSub = 2;
                    } else {
                        pNeighbour = pfnLookup((i32)nCellX + 1, (i32)nCellY + 1, (i32)nLayer, pUser);
                        if (pNeighbour == NULL || pNeighbour->pUnit != pUnit) {
                            nSub = 1;
                        } else {
                            nSub = 0;
                        }
                    }
                }
                nRowBase = nSub + s_aUnitRowBase[PUFO_UNITLOFT_ROWS_PER_TYPE * pUnit->nUnitType
                                                 + 2 * pUnit->bFacing];
            } else {
                nRowBase = s_aUnitRowBase[PUFO_UNITLOFT_ROWS_PER_TYPE * pUnit->nUnitType
                                          + 2 * pUnit->bFacing
                                          + ((pUnit->bFlags & 1u) != 0 ? 1 : 0)];
            }

            /* row = y % 16 + 16 * (12 * rowBase + z % 12) */
            nRow = nYSub + PUFO_VOX_PER_CELL_Y
                          * (PUFO_VOX_PER_CELL_Z * nRowBase + nZSub);
            if (Los_TestRowBit(pTables->pUnitloft, pTables->nUnitloft, nRow, nXBit)) {
                return LOS_HIT_OCCUPANT;
            }
        }
    }

    return LOS_HIT_NONE;
}

/* The original's rough position key for detecting a cell change:
 *     idx = (int)(x * 0.0625) + 60 * (int)(-y * 0.0625) + 3600 * (int)(-z * 0.0625)
 * i.e. /16 on all three axes. Note this divides z by 16 even though a cell is only
 * 12 voxels tall, so the "layer" term lags the real layer by one cell per 48
 * voxels. That inconsistency is in the original (Map_TraceVoxelOccupancy divides
 * z by 12); it only affects which cell's cost bits are accumulated, not the
 * blocking test, so it is reproduced rather than corrected.
 *
 * The original indexes apCells[0][0][idx] -- the grid flat. The cell is recovered
 * here by decomposing the index so an injected lookup can be used. */
static i32 Los_FlatIndex(i32 nVoxX, i32 nVoxY, i32 nVoxZ)
{
    i32 nX = (i32)(nVoxX * 0.0625);
    i32 nY = (i32)(nVoxY * -0.0625);
    i32 nZ = (i32)(nVoxZ * -0.0625);

    return nX + 60 * (nY + 60 * nZ);
}

static MapCell *Los_CellFromFlat(MapCellLookup pfnLookup, void *pUser, i32 nFlat)
{
    i32 nLayer;
    i32 nRem;

    if (nFlat < 0) {
        return NULL;
    }
    nLayer = nFlat / (PUFO_CELL_GRID_MAX * PUFO_CELL_GRID_MAX);
    nRem   = nFlat % (PUFO_CELL_GRID_MAX * PUFO_CELL_GRID_MAX);
    if (nLayer >= PUFO_MAP_LAYERS) {
        return NULL;
    }
    return pfnLookup(nRem % PUFO_CELL_GRID_MAX, nRem / PUFO_CELL_GRID_MAX, nLayer, pUser);
}

/* Map_TraceLos @ 0x30890.
 *
 * The step is one voxel per iteration, so `dTravel` counts voxels walked. The
 * ray stops when a voxel blocks, when the accumulated cell cost passes 7, or when
 * it reaches the bound; reaching the bound is the only path that returns true.
 *
 * The purely-vertical case (dx == 0 and dy == 0) deliberately randomises yaw from
 * Game_RandU16(62) * 0.1 and snaps pitch to +/-pi/2. That is the original's
 * behaviour, not a placeholder. */
bool Map_TraceLos(const LosTables *pTables, MapCellLookup pfnLookup, void *pUser,
                  i32 nX0, i32 nY0, i32 nZ0,
                  i32 nX1, i32 nY1, i32 nZ1,
                  u8 *pOutYawRandom, i32 *pOutCost)
{
    double dYaw;
    double dPitch;
    double dDist;
    double dBound;
    double dTravel;
    i32    nDx = nX1 - nX0;
    i32    nDy = nY1 - nY0;
    i32    nDz = nZ1 - nZ0;
    i32    nCost = 0;
    i32    nPrevFlat = -1;

    if (pOutYawRandom != NULL) {
        *pOutYawRandom = 0;
    }
    if (pOutCost != NULL) {
        *pOutCost = 0;
    }

    if (nDx != 0 || nDy != 0) {
        dYaw = atan2((double)nDy, (double)nDx);
        dDist = sqrt((double)(nDx * nDx) + (double)(nDy * nDy));
        dPitch = atan2((double)nDz, dDist);
    } else {
        /* Vertical ray. */
        u8 nRand;

        dPitch = (nZ1 <= nZ0) ? -1.57075 : 1.57075;
        nRand = Game_RandU16(0x3E);
        if (pOutYawRandom != NULL) {
            *pOutYawRandom = nRand;
        }
        dYaw = (double)nRand * 0.1;
        dDist = 0.0;
    }

    /* bound = sqrt(dz*dz + dist*dist) */
    dBound = sqrt((double)(nDz * nDz) + dDist * dDist);

    dTravel = 0.0;
    for (;;) {
        i32 nFx;
        i32 nFy;
        i32 nFz;
        i32 nFlat;
        i32 nCostBits;

        dTravel += 1.0;
        nFx = (i32)(dTravel * cos(dPitch) * cos(dYaw)) + nX0;
        nFy = (i32)(dTravel * cos(dPitch) * sin(dYaw)) + nY0;
        nFz = (i32)(dTravel * sin(dPitch)) + nZ0;

        /* Cell-change cost accumulation. Bits 0..2 of nFlags32 count, except the
         * values 0 and 4 which the original skips. */
        nFlat = Los_FlatIndex(nFx, nFy, nFz);
        if (nFlat != nPrevFlat) {
            MapCell *pCell = Los_CellFromFlat(pfnLookup, pUser, nFlat);

            nPrevFlat = nFlat;
            if (pCell != NULL) {
                nCostBits = (i32)(pCell->nFlags32 & 7u);
                if (nCostBits != 0 && nCostBits != 4) {
                    nCost += nCostBits;
                }
                if (nCost > 7) {
                    break;
                }
            }
        }

        /* A blocked voxel ends the trace. */
        if (nFx < 0 || nFy < 0 || nFz < 0) {
            break;
        }
        if (Map_TraceVoxelOccupancy(pTables, pfnLookup, pUser,
                                    (u32)nFx, (u32)nFy, (u32)nFz,
                                    NULL, NULL) != LOS_HIT_NONE) {
            break;
        }

        if (dTravel >= dBound) {
            if (pOutCost != NULL) {
                *pOutCost = nCost;
            }
            return true;
        }
    }

    if (pOutCost != NULL) {
        *pOutCost = nCost;
    }
    return false;
}
