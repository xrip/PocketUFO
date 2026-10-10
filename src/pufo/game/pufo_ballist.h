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
 *   0x1406C  bFireHitResult (char), only written by Tact_ProcessFireAction
 *   0x1406D  bTrajHitStop   (char), the loop flag read by Tact_ResolveThrowLanding
 *   0x14078  arc denominator  (double, Tact_SimulateThrowArc)
 *   0x14080  nTrajBound       \ the bound double, stored as two dwords
 *   0x14084  nTrajBoundHi     /
 *   0x14088  aim row id (nFlags)
 *
 * Offsets are read straight from the IDB's `Map` type (152632 bytes) with
 * `read_struct`, and `adMtX/adMtY/adMtZ` at 0x13EE8/0x13F38/0x13F88 are
 * `double[10]`, not single values. Only index 0 is used by the aim, arc and
 * landing functions read so far.
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
    bool   bLeftMap;      /* the sample failed the bounds test (port-local flag) */
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

/* Result of Tact_SimulateThrowArc @ 0x3102c. The first group mirrors the Map
 * aim block the original writes: nLosX0/Y0/Z0, nLosX1/Y1/Z1, dAngleYaw,
 * nTrajBound. Tact_ResolveThrowLanding reads them back. */
typedef struct ThrowArc {
    i32    nOriginX;      /* nLosX0 @ 0x14004 */
    i32    nOriginY;      /* nLosY0 @ 0x14008 */
    i32    nOriginZ;      /* nLosZ0 @ 0x1400C */
    i32    nTargetX;      /* nLosX1 @ 0x14010, possibly randomised */
    i32    nTargetY;      /* nLosY1 @ 0x14014, possibly randomised */
    i32    nTargetZ;      /* nLosZ1 @ 0x14018 */
    double dBound;        /* nTrajBound @ 0x14080, hypot of the horizontal run */
    double dYaw;          /* dAngleYaw @ 0x14028, atan2(dy, dx) */
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

/* ## Tact_ResolveThrowLanding @ 0x31a18
 *
 * Range budget inputs, all read straight off the records in the original:
 *
 *   nStatThrow    Soldier.auStat[8]  (LDRH [R1,#0xB2])
 *   nStatStrength Soldier.auStat[9]  (LDRH [R1,#0xB4])
 *   nWeight       ObjectRecFull.bWeight @ 0xA5 (LDRB [R2,#0xA5])
 *   nStunOrArmor  Soldier.uStunOrArmor @ 0x8   (LDRH [R1,#8])
 */
typedef struct ThrowBudget {
    i32 nStatThrow;
    i32 nStatStrength;
    i32 nWeight;
    i32 nStunOrArmor;
} ThrowBudget;

#define PUFO_THROW_STAT_THROW    8   /* auStat index */
#define PUFO_THROW_STAT_STRENGTH 9   /* auStat index */
#define PUFO_THROW_RANGE_SCALE   0.0625  /* 1/16, cell distance to range units */
#define PUFO_THROW_Z_DIV         8       /* integer divisor on the height gain */
#define PUFO_THROW_BUDGET_MIN    1.0
#define PUFO_THROW_BUDGET_MAX    22.0
#define PUFO_THROW_BUMP_BOUND    0.01
#define PUFO_THROW_BUMP_PLAIN    0.5
#define PUFO_THROW_Z_MAX         47.0
#define PUFO_THROW_LAND_MARGIN   8.0

/* The budget is a compiler artifact turned into behaviour, and the disassembly
 * at 0x31a44..0x31a78 shows exactly how:
 *
 *   MUL  R0, auStat[9], bWeight          ; w
 *   MUL  R1, auStat[8], uStunOrArmor     ; s
 *   BL   _rt_sdiv                        ; R0 = s/w, R1 = s%w
 *   MOVS R3, R0 / ADDMI R3, R3, #1      ; round the quotient toward +inf
 *   MOV  R0, R3, ASR #1                 ; R0 = quotient / 2
 *   BL   _itod                          ; converts the 64-bit pair R1:R0
 *
 * _itod takes a 64-bit integer in R1:R0, and R1 still holds the remainder left
 * by _rt_sdiv. So the high half of the value is the remainder, not a chosen
 * quantity. The consequence is a two-case rule:
 *
 *   s % w != 0  ->  the value is at least 2^32  ->  clamped to 22.0
 *   s % w == 0  ->  the value is (s/w)/2        ->  clamped into [1.0, 22.0]
 *
 * pQuot and pRem are optional outputs so a test can see the raw division. */
double Throw_RangeBudget(const ThrowBudget *pB, i32 *pQuot, i32 *pRem);

/* Outcome of Tact_ResolveThrowLanding. dMtX/Y/Z is the landing point in voxels. */
typedef struct ThrowLanding {
    double dTravel;      /* distance walked along the arc */
    double dMtX;
    double dMtY;
    double dMtZ;
    bool   bHitStop;     /* final value of Map.bTrajHitStop @ 0x1406D */
    bool   bLanded;      /* the throw reached its own bound */
    bool   bRejected;    /* the pre-arc range test failed, nothing was solved */
} ThrowLanding;

/* Tact_ResolveThrowLanding @ 0x31a18.
 *
 * Returns 0 when the throw cannot reach the target, or when the walked distance
 * is out of budget at the end; otherwise the "landed" flag. bHitStopIn is the
 * Map flag the caller leaves behind: the loop always runs once, but the flag
 * decides whether it runs again. The original's only writers of that flag are
 * the input handlers, Tact_ProcessFireAction and Tact_ProcessFallAction.
 *
 * The target jitter is applied twice on purpose, once here for the range test
 * and again inside Tact_SimulateThrowArc, each with its own draws. */
i32 Tact_ResolveThrowLanding(ThrowArc *pArc, ThrowLanding *pOut,
                             const LosTables *pTables, const MapGeom *pGeom,
                             MapCellLookup pfnLookup, void *pUser,
                             const Soldier *pUnitSelExclude,
                             const Soldier *pUnitExtraExclude,
                             const ThrowBudget *pBudget, u8 nMapDimX,
                             bool bHitStopIn,
                             i32 nX0, i32 nY0, i32 nZ0,
                             i32 nX1, i32 nY1, i32 nZ1);

#endif /* PUFO_BALLIST_H */
