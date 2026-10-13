/* pufo_ballist.c - see pufo_ballist.h for the evidence and the block layout. */
#include "game/pufo_ballist.h"
#include "game/pufo_unit.h"    /* Soldier, g_aUnitMuzzleOffset */
#include "util/pufo_rand.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const u8 g_atAimRowTable[PUFO_AIM_ROW_TABLE_SIZE] = {
    0x03, 0x04, 0x05, 0x05, 0x06, 0x00, 0x05, 0x00, 0x06, 0x00, 0x01,
    0x11, 0x12, 0x13, 0x13, 0x14, 0x15, 0x00, 0x02, 0x00, 0x02, 0x00,
    0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x02,
    0x0d, 0x0e, 0x0e, 0x0e, 0x0e, 0x0f, 0x0f, 0x0f, 0x00, 0x00, 0x03,
    0x01, 0x02, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00,
    0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* The arc stops when the sample reaches this height (0x31... constant pair
 * 0x4043000000000000 read by Tact_TraceProjectileArc @ 0x2fd30). */
#define PUFO_ARC_Z_LIMIT 38.0
/* Map volume limit for x and y: the arc aborts once the sample reaches
 * 160 * nMapDimX (the original tests ltd(x, 160*nMapDimX) == 0). */
#define PUFO_ARC_XY_SCALE 160.0

/* The original divides voxel x and y by 16 with MOVS/ADDMI +0xF then ASR #4. That is truncating
 * division, the same as C's /, not floor: for v < 0, (v + 15) >> 4 equals v / 16. z uses _rt_sdiv,
 * which truncates too. MapView_DrawUnitFacingArrow @ 0x1b7bc divides the roster the same way, so
 * this one is shared. See docs/parity/bugs.tsv at 0x1B8EC for the naming finding. */
i32 Ball_FloorDiv16(i32 nV)
{
    return (nV >= 0) ? (nV >> 4) : ((nV + 15) >> 4);
}

static i32 Ball_Trunc(i32 nV, i32 nDiv)
{
    return nV / nDiv;
}

void Aim_ComputeAngles(i32 nDx, i32 nDy, i32 nDz,
                       double *pYaw, double *pPitch, double *pDist,
                       u8 *pYawRand)
{
    if (nDx != 0 || nDy != 0) {
        double dDist;

        *pYaw = atan2((double)nDy, (double)nDx);
        dDist = sqrt((double)(nDy * nDy) + (double)(nDx * nDx));
        *pDist = dDist;
        *pPitch = atan2((double)nDz, dDist);
        if (pYawRand != NULL) {
            *pYawRand = 0;
        }
    } else {
        /* Purely vertical: the original draws a random yaw and snaps pitch. */
        u8 nRand = (u8)Game_RandU16(PUFO_AIM_YAW_RAND_MAX);

        *pPitch = (nDz > 0) ? PUFO_AIM_HALF_PI : -PUFO_AIM_HALF_PI;
        *pYaw = (double)nRand * PUFO_AIM_YAW_RAND_SCALE;
        *pDist = 0.0;
        if (pYawRand != NULL) {
            *pYawRand = nRand;
        }
    }
}

/* Stamp bit 5 of bFlags29 on the voxel's cell, as both arc functions do.
 * The original indexes the cell grid flat (60 * layer + y) * 60 + x and only
 * Tact_ComputeAimVector checks the bounds first; Tact_SimulateThrowArc does not. */
static void Ball_StampOrigin(MapCellLookup pfnLookup, void *pUser,
                             const MapGeom *pGeom, bool bCheckBounds,
                             i32 nX0, i32 nY0, i32 nZ0)
{
    i32 nCellX = Ball_FloorDiv16(nX0);
    i32 nCellY = Ball_FloorDiv16(nY0);
    i32 nLayer = Ball_Trunc(nZ0, PUFO_VOX_PER_CELL_Z);
    MapCell *pCell;

    if (pfnLookup == NULL) {
        return;
    }
    if (bCheckBounds && pGeom != NULL
        && !Map_IsValidCell(pGeom, nCellX, nCellY, nLayer)) {
        return;
    }
    pCell = pfnLookup(nCellX, nCellY, nLayer, pUser);
    if (pCell != NULL) {
        pCell->bFlags29 |= 0x20u;
    }
}

void Tact_ComputeAimVector(AimState *pAim, const MapGeom *pGeom,
                           MapCellLookup pfnLookup, void *pUser,
                           i32 nX0, i32 nY0, i32 nZ0,
                           i32 nX1, i32 nY1, i32 nZ1, u8 nRowId)
{
    double dDist;
    i32 nDx = nX1 - nX0;
    i32 nDy = nY1 - nY0;
    i32 nDz = nZ1 - nZ0;

    pAim->nOriginX = nX0;
    pAim->nOriginY = nY0;
    pAim->nOriginZ = nZ0;
    pAim->dTravel = 0.0;
    pAim->nTargetX = nX1;
    pAim->nTargetY = nY1;
    pAim->nTargetZ = nZ1;

    Ball_StampOrigin(pfnLookup, pUser, pGeom, true, nX0, nY0, nZ0);

    pAim->nRowId = (i32)nRowId;
    for (i32 i = 0; i < PUFO_AIM_ROW_COUNT; ++i) {
        pAim->anAimRow[i] = (nRowId <= PUFO_AIM_ROW_MAX_ID)
                          ? (i32)g_atAimRowTable[PUFO_AIM_ROW_COUNT * nRowId + i]
                          : 0;
    }

    Aim_ComputeAngles(nDx, nDy, nDz, &pAim->dYaw, &pAim->dPitch, &dDist, NULL);
    pAim->bYawFromRandom = (nDx == 0 && nDy == 0);

    /* bound = sqrt(dz*dz + dist*dist); dist is 0 in the vertical case. */
    pAim->dBound = sqrt((double)(nDz * nDz) + dDist * dDist);
}

bool Tact_TraceProjectileArc(AimState *pAim, const LosTables *pTables,
                             const MapGeom *pGeom, MapCellLookup pfnLookup,
                             void *pUser, const Soldier *pUnitSelExclude,
                             const Soldier *pUnitExtraExclude,
                             i32 nX0, i32 nY0, i32 nZ0,
                             i32 nX1, i32 nY1, i32 nZ1, u8 nRowId,
                             ShotTrace *pOut)
{
    ShotTrace tr;
    double dX, dY, dZ;
    double dLimit;
    bool bOutOfRange = false;
    bool bRunning = true;

    Tact_ComputeAimVector(pAim, pGeom, pfnLookup, pUser,
                          nX0, nY0, nZ0, nX1, nY1, nZ1, nRowId);

    /* The running position doubles, adMtX/Y/Z[0]. */
    dX = (double)nX0;
    dY = (double)nY0;
    dZ = (double)nZ0;

    dLimit = PUFO_ARC_XY_SCALE * (double)(pGeom != NULL ? pGeom->nDimX : 0);

    tr.nOrigX = 0;
    tr.nOrigY = 0;
    tr.nTgtX = 0;
    tr.nTgtY = 0;
    tr.nTgtZ = 0;
    tr.nEndX = 0;
    tr.nEndY = 0;
    tr.nEndZ = 0;
    tr.dTravelSave = 0.0;
    tr.bOutOfRange = false;
    tr.bLeftMap = false;

    do {
        pAim->dTravel += 1.0;

        /* Sample of the position before this step. */
        tr.nOrigX = (i32)(dX / (double)PUFO_VOX_PER_CELL_X);
        tr.nOrigY = (i32)(dY / (double)PUFO_VOX_PER_CELL_Y);

        dX = pAim->dTravel * cos(pAim->dPitch) * cos(pAim->dYaw) + (double)nX0;
        dY = pAim->dTravel * cos(pAim->dPitch) * sin(pAim->dYaw) + (double)nY0;
        dZ = pAim->dTravel * sin(pAim->dPitch) + (double)nZ0;

        if (!(dX >= 0.0) || !(dY >= 0.0) || !(dZ >= 0.0)
            || dX >= dLimit || dY >= dLimit || dZ >= PUFO_ARC_Z_LIMIT) {
            bRunning = false;
            tr.bLeftMap = true;
        }

        /* The original tests the occupancy result truncated to one byte:
         *     if ((u8)Map_TraceVoxelOccupancy(...) != 0) continue;
         *     else { record target; stop; }
         * A clear voxel returns LOS_HIT_NONE (-1), which is non-zero as a byte,
         * so the walk continues; only a result of 0 (LOS_HIT_FLOOR) stops it.
         * That is the original's rule, reproduced literally. */
        if ((u8)Map_TraceVoxelOccupancy(pTables, pfnLookup, pUser,
                                        (u32)(i32)dX, (u32)(i32)dY, (u32)(i32)dZ,
                                        pUnitSelExclude, pUnitExtraExclude) != 0) {
            /* Keep walking. */
        } else {
            tr.nTgtX = (i32)(dX / (double)PUFO_VOX_PER_CELL_X);
            tr.nTgtY = (i32)(dY / (double)PUFO_VOX_PER_CELL_Y);
            tr.nTgtZ = (i32)(dZ / (double)PUFO_VOX_PER_CELL_Z);
            bRunning = false;
        }

        if (pAim->dTravel >= pAim->dBound) {
            bOutOfRange = true;
        }
    } while (bRunning);

    tr.dTravelSave = pAim->dTravel;
    tr.nEndX = (i32)dX;
    tr.nEndY = (i32)dY;
    tr.nEndZ = (i32)dZ;
    tr.bOutOfRange = bOutOfRange;

    if (pOut != NULL) {
        *pOut = tr;
    }
    return bOutOfRange;
}

/* Height of the throw arc when it reaches horizontal distance dDist.
 * Evidence: 0x318b8..0x31930, dist*tan(t) - (g*dist^2) / (2*cos(t)^2*v0^2). */
static double Throw_HeightAt(double dDist, double dTheta, double dVelSq)
{
    double dCos = cos(dTheta);

    return dDist * tan(dTheta)
         - (dDist * dDist * PUFO_THROW_GRAVITY)
         / (2.0 * dCos * dCos * dVelSq);
}

void Tact_SimulateThrowArc(ThrowArc *pOut, const MapGeom *pGeom,
                           MapCellLookup pfnLookup, void *pUser,
                           i32 nX0, i32 nY0, i32 nZ0,
                           i32 nX1, i32 nY1, i32 nZ1)
{
    i32 nTx = nX1;
    i32 nTy = nY1;
    i32 nDx, nDy, nDz;
    double dDist;
    double dNum;
    double dBase = 0.001;
    double dStep = PUFO_AIM_HALF_PI;
    double dVelSq = 0.0;
    double dArcDenom = 0.0;
    double dPitch = 0.0;
    bool bLanded = false;

    /* A target this close is pushed 2..4 tiles away on each axis first. */
    if (abs(nX1 - nX0) < 3 && abs(nY1 - nY0) < 3) {
        nTx = (Game_RandU16(2) != 0)
            ? nX1 - (i32)Game_RandU16(3) - 2
            : nX1 + (i32)Game_RandU16(3) + 2;
        nTy = (Game_RandU16(2) != 0)
            ? nY1 - (i32)Game_RandU16(3) - 2
            : nY1 + (i32)Game_RandU16(3) + 2;
    }

    Ball_StampOrigin(pfnLookup, pUser, pGeom, false, nX0, nY0, nZ0);

    nDx = nTx - nX0;
    nDy = nTy - nY0;
    nDz = nZ1 - nZ0;
    dDist = sqrt((double)(nDy * nDy) + (double)(nDx * nDx));

    /* Height term: dz + 3 when throwing up, otherwise 3. */
    dNum = 2.0 * (double)((nDz > 0) ? (nDz + 3) : 3) * PUFO_THROW_GRAVITY;

    for (;;) {
        bool bRefine = false;
        double dRefinePitch = 0.0;

        for (i32 i = 0; i < 19; ++i) {
            double dTheta = dBase + dStep * 0.05 * (double)i;
            double dThetaNext = dBase + dStep * 0.05 * (double)(i + 1);
            double dSin = sin(dTheta);
            double dSinNext = sin(dThetaNext);
            double dDenomNow;
            double dDenomNext;
            i32 nHeightNow;
            i32 nHeightNext;

            if (dSin == 0.0 || dSinNext == 0.0) {
                continue;
            }

            dDenomNow = sqrt(dNum / (dSin * dSin));
            dDenomNext = sqrt(dNum / (dSinNext * dSinNext));
            dArcDenom = dDenomNow;
            dPitch = dTheta;
            dVelSq = dDenomNow * dDenomNow;

            /* Range of the next candidate must not overshoot the target. */
            if (sin(2.0 * dThetaNext) * dDenomNext * dDenomNext
                * (1.0 / (2.0 * PUFO_THROW_GRAVITY)) > (double)dDist) {
                continue;
            }

            nHeightNow = (i32)Throw_HeightAt(dDist, dTheta,
                                             dDenomNow * dDenomNow);
            nHeightNext = (i32)Throw_HeightAt(dDist, dThetaNext,
                                              dDenomNext * dDenomNext);

            if (nDz <= nHeightNow) {
                if (nHeightNow <= nDz + 1) {
                    bLanded = true;
                    goto done;
                }
                if (nDz < nHeightNow && nDz > nHeightNext) {
                    bRefine = true;
                    dRefinePitch = dTheta;
                    break;
                }
            }
            if (nDz < nHeightNext && nDz > nHeightNow) {
                bRefine = true;
                dRefinePitch = dTheta;
                break;
            }
        }

        if (!bRefine) {
            break;
        }
        dBase = dRefinePitch;
        dStep *= 0.05;
    }

done:
    if (pOut != NULL) {
        pOut->nOriginX = nX0;
        pOut->nOriginY = nY0;
        pOut->nOriginZ = nZ0;
        pOut->nTargetX = nTx;
        pOut->nTargetY = nTy;
        pOut->nTargetZ = nZ1;
        pOut->dBound = dDist;
        pOut->dYaw = atan2((double)nDy, (double)nDx);
        pOut->dArcDenom = dArcDenom;
        pOut->dLaunchVelSq = dVelSq;
        pOut->dPitch = dPitch;
        pOut->bLanded = bLanded;
    }
}

/* Throw_RangeBudget - see pufo_ballist.h for the disassembly it reproduces. */
double Throw_RangeBudget(const ThrowBudget *pB, i32 *pQuot, i32 *pRem)
{
    i32 nW = pB->nStatStrength * pB->nWeight;
    i32 nS = pB->nStatThrow * pB->nStunOrArmor;
    i32 nQuot;
    i32 nRem;
    i32 nHalf;
    double dBudget;

    /* _rt_sdiv with a zero divisor has no defined result. The port takes the
     * helper's usual behaviour, quotient 0 and remainder the dividend, which
     * lands on the 1.0 floor. Recorded as a deviation, not as fact. */
    if (nW == 0) {
        nQuot = 0;
        nRem = nS;
    } else {
        nQuot = nS / nW;
        nRem = nS % nW;
    }

    /* MOVS/ADDMI adds 1 to a negative quotient, so it rounds toward zero before
     * the arithmetic shift. */
    nHalf = (nQuot + ((nQuot < 0) ? 1 : 0)) >> 1;

    /* _itod converts the 32-bit int in R0. The remainder in R1 is dead. */
    dBudget = (double)nHalf;
    if (dBudget < PUFO_THROW_BUDGET_MIN) {
        dBudget = PUFO_THROW_BUDGET_MIN;
    } else if (dBudget > PUFO_THROW_BUDGET_MAX) {
        dBudget = PUFO_THROW_BUDGET_MAX;
    }

    if (pQuot != NULL) {
        *pQuot = nQuot;
    }
    if (pRem != NULL) {
        *pRem = nRem;
    }
    return dBudget;
}

/* Landing height of the solved arc at a walked distance, the same expression
 * Tact_SimulateThrowArc uses inside its search. */
static double Throw_LandingZ(double dTravel, double dPitch, double dDenom,
                             double dZ0)
{
    double dCos = cos(dPitch);
    double dNumer = dTravel * dTravel * PUFO_THROW_GRAVITY;
    double dDenomFull = dDenom * dDenom * dCos * dCos * 2.0;

    return dZ0 + dTravel * tan(dPitch) - dNumer / dDenomFull;
}

i32 Tact_ResolveThrowLanding(ThrowArc *pArc, ThrowLanding *pOut,
                             const LosTables *pTables, const MapGeom *pGeom,
                             MapCellLookup pfnLookup, void *pUser,
                             const Soldier *pUnitSelExclude,
                             const Soldier *pUnitExtraExclude,
                             const ThrowBudget *pBudget, u8 nMapDimX,
                             bool bHitStopIn,
                             i32 nX0, i32 nY0, i32 nZ0,
                             i32 nX1, i32 nY1, i32 nZ1)
{
    ThrowArc arc;
    i32 nJitX = nX1;
    i32 nJitY = nY1;
    double dBudget;
    double dRange;
    double dTravel;
    double dX, dY, dZ;
    double dBound;
    bool bHitStop = bHitStopIn;
    bool bLanded = false;

    memset(&arc, 0, sizeof(arc));

    dBudget = Throw_RangeBudget(pBudget, NULL, NULL);

    /* A target this close is pushed 2..5 tiles away, once for this range test.
     * Tact_SimulateThrowArc repeats the jitter with its own draws. */
    if (abs(nX1 - nX0) < 3 && abs(nY1 - nY0) < 3) {
        nJitX = (Game_RandU16(2) != 0)
              ? nX1 - (i32)Game_RandU16(3) - 2
              : nX1 + (i32)Game_RandU16(3) + 2;
        nJitY = (Game_RandU16(2) != 0)
              ? nY1 - (i32)Game_RandU16(3) - 2
              : nY1 + (i32)Game_RandU16(3) + 2;
    }

    dRange = sqrt((double)(nJitY - nY0) * (nJitY - nY0)
                + (double)(nJitX - nX0) * (nJitX - nX0)) * PUFO_THROW_RANGE_SCALE;
    if (nZ1 >= nZ0) {
        /* Integer division: a climb of under 8 voxels earns no allowance. */
        dRange *= (double)((nZ1 - nZ0) / PUFO_THROW_Z_DIV);
    }
    if (dRange > dBudget) {
        if (pOut != NULL) {
            memset(pOut, 0, sizeof(*pOut));
            pOut->bRejected = true;
        }
        return 0;
    }

    Tact_SimulateThrowArc(&arc, pGeom, pfnLookup, pUser,
                          nX0, nY0, nZ0, nX1, nY1, nZ1);

    dTravel = 0.0;   /* the arc setup writes 0.0 into Map.dTravel */
    dBound = arc.dBound;
    dX = (double)arc.nOriginX;
    dY = (double)arc.nOriginY;
    dZ = (double)arc.nOriginZ;

    for (;;) {
        i32 nDz = abs(arc.nTargetZ - arc.nOriginZ);

        if (dBound < (double)nDz) {
            dTravel += (dBound / (double)nDz) * PUFO_THROW_BUMP_BOUND;
        } else {
            dTravel += PUFO_THROW_BUMP_PLAIN;
        }

        dX = (double)arc.nOriginX + dTravel * cos(arc.dYaw);
        dY = (double)arc.nOriginY + dTravel * sin(arc.dYaw);
        dZ = Throw_LandingZ(dTravel, arc.dPitch, arc.dArcDenom,
                            (double)arc.nOriginZ);

        if (dX <= 0.0 || dY <= 0.0 || dZ <= 0.0
            || dX >= PUFO_ARC_XY_SCALE * (double)nMapDimX
            || dY >= PUFO_ARC_XY_SCALE * (double)nMapDimX
            || dZ >= PUFO_THROW_Z_MAX) {
            bHitStop = false;
        }

        if (dTravel >= dBound - PUFO_THROW_LAND_MARGIN) {
            bLanded = true;
        }

        /* Only LOS_HIT_FLOOR (0) clears the flag; a clear voxel returns -1, and
         * the original masks the result to a byte before comparing. */
        if (Map_TraceVoxelOccupancy(pTables, pfnLookup, pUser,
                                    (u32)(i32)dX, (u32)(i32)dY, (u32)(i32)dZ,
                                    pUnitSelExclude, pUnitExtraExclude) == LOS_HIT_FLOOR) {
            bHitStop = false;
        }

        if (!bHitStop) {
            break;
        }
    }

    if (pOut != NULL) {
        pOut->dTravel = dTravel;
        pOut->dMtX = dX;
        pOut->dMtY = dY;
        pOut->dMtZ = dZ;
        pOut->bHitStop = bHitStop;
        pOut->bLanded = bLanded;
        pOut->bRejected = false;
    }
    if (pArc != NULL) {
        *pArc = arc;
    }

    if (dTravel * PUFO_THROW_RANGE_SCALE <= dBudget) {
        return bLanded ? 1 : 0;
    }
    return 0;
}

/* Unit_RollChance @ 0x25e6c.
 *
 * The soft-FPU thunks force a mixed-precision chain that is worth keeping
 * literal, because dropping a step changes the rounded magnitude:
 *   _muld  (double) dBound * 0.27
 *   _dtos  -> float
 *   _itos  (nMiss + 10) -> float
 *   _muls  float * float
 *   _muls  float * 0.01f
 *   _stod  -> double
 * The two angles come from _rt_sdiv, which takes the divisor in R0 and the
 * dividend in R1, so each angle is a whole radian 0..6. */
i32 Unit_RollChance(double dBound, i32 nChance, ScatterOffset *pScatter)
{
    i32 nRoll = (i32)Game_RandU16(100) + 1;

    if (nRoll <= nChance) {
        pScatter->nDx = 0;
        pScatter->nDy = 0;
        pScatter->nDz = 0;
        return 0;
    }

    /* The miss width is masked to 16 bits by LSL#16/LSR#16 before it is used
     * as nMax, which is what the u16 cast reproduces. */
    i32 nMiss = (i32)Game_RandU16((u16)(nRoll - nChance));

    float  fMag = (float)(dBound * 0.27);
    double dMag;
    i32    nAngA;
    i32    nAngB;

    fMag *= (float)(nMiss + 10);
    fMag *= 0.01f;
    dMag = (double)fMag;

    /* Both draws happen before any trig call, so the stream order is fixed. */
    nAngA = (i32)Game_RandU16(628) / 100;
    nAngB = (i32)Game_RandU16(628) / 100;

    /* _dtoi truncates toward zero, which is what a C cast does. */
    pScatter->nDx = (i32)(cos((double)nAngA) * dMag);
    pScatter->nDy = (i32)(sin((double)nAngA) * dMag);
    pScatter->nDz = (i32)(sin((double)nAngB) * dMag);

    return pScatter->nDz;
}

/* --- pieces of Tact_ProcessMoveAction @ 0x4f3f0 --- */

MuzzleOffset Tact_MuzzleOffset(const Soldier *pUnit, const ObjectRec *pObj)
{
    i32 nType = (i32)pUnit->nUnitType;
    if (nType >= PUFO_UNIT_MUZZLE_TYPE_COUNT) {
        nType = PUFO_UNIT_MUZZLE_TYPE_COUNT - 1;
    }

    i32 nSel = ((i32)pObj->bListPri > 1) ? 0 : (i32)pObj->bListPri;
    i32 nFacing = (i32)pUnit->bFacing & 7;

    return g_aUnitMuzzleOffset[nType][nSel][nFacing];
}

void Tact_ChargeShotAmmo(TactItem *pWeapon, i32 *pnShotCount)
{
    const ObjectRec *pObj = pWeapon->pObj;
    const u16 uKey = pObj->uSortKey;
    const bool bSpecial = (uKey == 13 || uKey == 35);

    if (pObj->bAmmoCap != 0xFF          /* 0x502E0 */
        && pWeapon->nAmmo != 0          /* 0x502EC */
        && !(bSpecial && *pnShotCount != 0)) { /* 0x502FC..0x50314 */
        pWeapon->nAmmo = (u8)(pWeapon->nAmmo + 0xFF); /* 0x50318 */
    }
    *pnShotCount += 1;                  /* 0x50320..0x50328 */
}
