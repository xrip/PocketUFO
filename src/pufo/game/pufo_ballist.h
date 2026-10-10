/* pufo_ballist.h - aim setup, projectile arc, throw arc.
 *
 * Evidence: Tact_ComputeAimVector @ 0x30368, Tact_TraceProjectileArc @ 0x2fc38,
 *           Tact_SimulateThrowArc @ 0x3102c, atAimRowTable @ 0xA9218.
 *
 * ## The Map aim block
 *
 * The original keeps all shot state in one block of the Map context. Offsets are
 * from the disassembly of Tact_ComputeAimVector (every write is an immediate
 * offset from the Map base register):
 *
 *   0x13FD8  11 dwords, copied from atAimRowTable[11 * nRowId + i]
 *   0x14004  origin X        0x14010 target X
 *   0x14008  origin Y        0x14014 target Y
 *   0x1400C  origin Z        0x14018 target Z
 *   0x14020  dTravel         (double, walked distance)
 *   0x14028  dAngleYaw       (double)
 *   0x14030  dAnglePitch     (double)
 *   0x14038  dTravelSave     (double, distance actually walked)
 *   0x14060  trajectory end X (int)
 *   0x14064  trajectory end Y (int)
 *   0x14068  trajectory end Z (int)
 *   0x14078  arc denominator  (double, Tact_SimulateThrowArc)
 *   0x14080  nTrajBound       \ the bound double, stored as two dwords
 *   0x14084  nTrajBoundHi     /
 *   0x14088  aim row id (nFlags)
 *
 * Aim row table (atAimRowTable @ 0xA9218, 6 rows x 11 bytes):
 *
 *   03 04 05 05 06 00 05 00 06 00 01
 *   11 12 13 13 14 15 00 02 00 02 00
 *   07 07 07 07 07 07 07 07 07 07 02
 *   0d 0e 0e 0e 0e 0f 0f 0f 00 00 03
 *   01 02 02 00 02 00 02 00 02 00 00
 *   09 00 00 00 00 00 00 00 00 00 00
 *
 * Only entry 10 is read outside the shot code: MapView_DrawUnitHeightGlyph
 * @ 0x1c240 uses it as a glyph id (`10 * anAimRow[10] + ...`) and skips the
 * glyph when it is 0. The other ten entries have no reader found yet; they are
 * recorded rather than interpreted.
 *
 * ## Units
 *
 * Coordinates in the aim block are voxels: 16 x 16 voxels per tile, 12 voxel
 * levels per tile. Conversions seen in the arc code are /16 on x and y and /12
 * on z. The original divides x and y with a floor (add 15 when negative, then
 * arithmetic shift) and z with C-style truncating division; both are reproduced.
 */
#ifndef PUFO_BALLIST_H
#define PUFO_BALLIST_H

#include "game/pufo_los.h"
#include "game/pufo_mapcell.h"
#include "pufo_common.h"

#define PUFO_AIM_ROW_COUNT   11
#define PUFO_AIM_ROW_TABLE_SIZE 66   /* 6 rows x 11 bytes */
#define PUFO_AIM_ROW_MAX_ID   5

/* The original's half-turn constant is 1.57075, not the exact pi/2. */
#define PUFO_AIM_HALF_PI     1.57075
/* Throw ballistics constant, 0x3FB28F5C... as encoded at 0x3102c. */
#define PUFO_THROW_GRAVITY   9.18
/* Random yaw spread for a vertical shot: Game_RandU16(0x3E) * 0.1. */
#define PUFO_AIM_YAW_RAND_MAX 0x3E
#define PUFO_AIM_YAW_RAND_SCALE 0.1

/* atAimRowTable @ 0xA9218. */
extern const u8 g_atAimRowTable[PUFO_AIM_ROW_TABLE_SIZE];

/* Shot state, one copy of the Map aim block. */
typedef struct AimState {
    i32    nOriginX;      /* 0x14004 */
    i32    nOriginY;      /* 0x14008 */
    i32    nOriginZ;      /* 0x1400C */
    i32    nTargetX;      /* 0x14010 */
    i32    nTargetY;      /* 0x14014 */
    i32    nTargetZ;      /* 0x14018 */
    double dTravel;       /* 0x14020 */
    double dYaw;          /* 0x14028 */
    double dPitch;        /* 0x14030 */
    double dBound;        /* 0x14080 / 0x14084 */
    i32    anAimRow[PUFO_AIM_ROW_COUNT]; /* 0x13FD8, bytes widened to dwords */
    i32    nRowId;        /* 0x14088 */
    bool   bYawFromRandom;
} AimState;

/* Angle setup shared by Tact_ComputeAimVector and Map_TraceLos.
 * When nDx == 0 and nDy == 0 the original randomises yaw and snaps pitch to
 * +/-PUFO_AIM_HALF_PI by the sign of nDz (up when nDz > 0). pYawRand receives
 * the raw 0..62 draw, or 0 when the angles are deterministic. */
void Aim_ComputeAngles(i32 nDx, i32 nDy, i32 nDz,
                       double *pYaw, double *pPitch, double *pDist,
                       u8 *pYawRand);

/* Tact_ComputeAimVector @ 0x30368.
 * Fills the aim block, copies the aim row, and stamps bit 5 of bFlags29 on the
 * origin cell (the original does it only when the origin cell is in bounds).
 * pfnLookup may be NULL, in which case the cell stamp is skipped. */
void Tact_ComputeAimVector(AimState *pAim, const MapGeom *pGeom,
                           MapCellLookup pfnLookup, void *pUser,
                           i32 nX0, i32 nY0, i32 nZ0,
                           i32 nX1, i32 nY1, i32 nZ1, u8 nRowId);

/* Result of Tact_TraceProjectileArc @ 0x2fc38. The fields map to the Map block
 * names in the header comment. */
typedef struct ShotTrace {
    i32    nOrigX;        /* 0x14054, first sample tile */
    i32    nOrigY;        /* 0x14058 */
    i32    nTgtX;         /* 0x14040, tile where the arc stopped */
    i32    nTgtY;         /* 0x14044 */
    i32    nTgtZ;         /* 0x14048 */
    i32    nEndX;         /* 0x14060, last voxel position */
    i32    nEndY;         /* 0x14064 */
    i32    nEndZ;         /* 0x14068 */
    double dTravelSave;   /* 0x14038 */
    bool   bOutOfRange;   /* true when the bound was passed */
    bool   bLeftMap;      /* 0x1406C, the arc left the map volume */
} ShotTrace;

/* Tact_TraceProjectileArc @ 0x2fc38.
 * Walks the arc one voxel per step (dTravel += 1.0) and stops when the sample
 * leaves the map volume, when a voxel blocks, or when dTravel passes the bound.
 * Returns the "out of range" flag. nWeaponField is the value the original reads
 * from apObjects[pSelWeapon->nIndex] + 0 into Map+0x14050; it is carried into
 * ShotTrace-free scratch only, so it is taken as an input the caller resolves. */
bool Tact_TraceProjectileArc(AimState *pAim, const LosTables *pTables,
                             const MapGeom *pGeom, MapCellLookup pfnLookup,
                             void *pUser, const Soldier *pUnitSelExclude,
                             const Soldier *pUnitExtraExclude,
                             i32 nX0, i32 nY0, i32 nZ0,
                             i32 nX1, i32 nY1, i32 nZ1, u8 nRowId,
                             ShotTrace *pOut);

/* Result of Tact_SimulateThrowArc @ 0x3102c. */
typedef struct ThrowArc {
    i32    nTargetX;      /* possibly randomised, see below */
    i32    nTargetY;
    double dArcDenom;     /* 0x14078 */
    double dLaunchVelSq;  /* v0^2 = 2 * PUFO_THROW_GRAVITY * height term */
    double dPitch;        /* pitch the search settled on */
    bool   bLanded;       /* true when an arc was found that lands at dz */
} ThrowArc;

/* Tact_SimulateThrowArc @ 0x3102c.
 *
 * Solves for a launch angle. The horizontal distance is dist = hypot(dx, dy)
 * and the height term is (dz > 0 ? dz + 3 : 3). For a candidate pitch the
 * launch speed is fixed by that height, v0^2 = 2 * g * heightTerm, and the arc
 * is accepted when its range at the next candidate angle does not exceed dist:
 *
 *   v0^2 * sin(2*theta) / (2*g) <= dist
 *
 * The search tries 19 angles, theta = base + step * 0.05 * i, starting at
 * base = 0.001 and step = PUFO_AIM_HALF_PI. When the trajectory heights at two
 * consecutive angles straddle dz it restarts with base = that angle and step
 * scaled by 0.05, which refines the bracket. Trajectory height at dist is
 *
 *   h = dist*tan(theta) - g*dist^2 / (2 * cos^2(theta) * v0^2)
 *
 * When |dx| < 3 and |dy| < 3 the original first pushes the target 2..4 tiles
 * away in a random direction on each axis; that is reproduced, and the moved
 * target is what the arc is solved for.
 */
void Tact_SimulateThrowArc(ThrowArc *pOut, const MapGeom *pGeom,
                           MapCellLookup pfnLookup, void *pUser,
                           i32 nX0, i32 nY0, i32 nZ0,
                           i32 nX1, i32 nY1, i32 nZ1);

#endif /* PUFO_BALLIST_H */
