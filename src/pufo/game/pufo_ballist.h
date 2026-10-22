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
 *   0x1406C  bFireHitResult (char), cleared at 0x2FC54 by every arc walk and set to 1
 *                           at 0x3010C when the walked sample leaves the map; read at
 *                           five sites in Tact_ProcessFireAction
 *   0x1406D  bTrajHitStop   (char), the throw gate: written by the input handlers
 *                           (Input_OnPointer, Input_OnConfirm, UiScreen_OnCancel, ...) and
 *                           by Tact_SimulateThrowArc at 0x31A0C; read by Tact_ProcessFireAction
 *                           cmd 0xE/0x11, Tact_ProcessFallAction and MapView_DrawUnitFacingArrow.
 *                           The action contexts call this byte ThrowPending.
 *   0x14070  pArrowThrownItem (TactItem*), written by Tact_SimulateThrowArc at 0x319FC
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
 * (0x1c240) uses it as a glyph id (`10 * anAimRow[10] + ...`) and skips the
 * glyph when it is 0. The other ten entries have no reader found yet; they are
 * recorded rather than interpreted.
 *
 * ## Units
 *
 * Coordinates in the aim block are voxels: 16 x 16 voxels per tile, 12 voxel
 * levels per tile. Conversions seen in the arc code are /16 on x and y and /12
 * on z. x and y use the add-15-then-arithmetic-shift idiom and z uses _rt_sdiv; both
 * truncate toward zero, so C's / reproduces them. The idiom is often called a floor and
 * for these operands it is not - see docs/parity/bugs.tsv at 0x1B8EC.
 */
#ifndef PUFO_BALLIST_H
#define PUFO_BALLIST_H

#include "game/pufo_los.h"
#include "game/pufo_mapcell.h"
#include "pufo_common.h"

#define PUFO_AIM_ROW_COUNT   11
#define PUFO_AIM_ROW_TABLE_SIZE 66   /* 6 rows x 11 bytes */
#define PUFO_AIM_ROW_MAX_ID   5

/* The Map keeps ten arc samples at 0x13EE8 / 0x13F38 / 0x13F88; the shot and
 * throw animation loops all run over exactly these ten. */
#define PUFO_AIM_SAMPLE_COUNT 10

/* The original's half-turn constant is 1.57075, not the exact pi/2. */
#define PUFO_AIM_HALF_PI     1.57075
/* Throw ballistics constant, 0x3FB28F5C... as encoded at 0x3102c. */
#define PUFO_THROW_GRAVITY   9.18
/* Random yaw spread for a vertical shot: Game_RandU16(0x3E) * 0.1. */
#define PUFO_AIM_YAW_RAND_MAX 0x3E
#define PUFO_AIM_YAW_RAND_SCALE 0.1

/* atAimRowTable @ 0xA9218. */
extern const u8 g_atAimRowTable[PUFO_AIM_ROW_TABLE_SIZE];

/* The original's voxel-to-cell divide on x and y: MOVS / ADDMI #0xF / ASR #4. That is TRUNCATING
 * division, not floor - for v < 0, (v + 15) >> 4 equals v / 16 exactly, so -17 gives -1 and not
 * -2. The name says floor and is kept because three call sites and the parity notes use it; the
 * finding is in docs/parity/bugs.tsv at 0x1B8EC. Shared with MapView_DrawUnitFacingArrow @ 0x1b7bc,
 * which divides the roster the same way. */
i32 Ball_FloorDiv16(i32 nV);

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
    /* The cell the arc stopped on, 0x14040 / 0x14044 / 0x14048. The shot trace
     * writes it and so do the four UI confirmations (Tact_HandleMapClick @ 0x1512c,
     * TactAction_ConfirmMeleeMode6 @ 0x17bb8, TactHud_UseSpecialItem20 @ 0x17d54,
     * TactAction_ConfirmSpecial9 @ 0x18260), which is why the original's name for
     * the slot is "melee cell". Tact_ProcessFireAction reads it back for
     * Combat_ResolveShot. */
    i32    nTgtX;
    i32    nTgtY;
    i32    nTgtZ;
    /* 0x14038: the distance the last arc walk actually took. Tact_ProcessFireAction
     * compares its own flight counter against it to decide when the shot lands. */
    double dTravelSave;
    /* The ten arc samples, 0x13EE8 / 0x13F38 / 0x13F88, in voxel units. NULL when
     * the caller has no Map block behind this AimState; the trace then keeps the
     * samples in its own locals. */
    double *padMtX;
    double *padMtY;
    double *padMtZ;
    /* The two shot-plumbing bytes that sit INSIDE this same block and that the arc
     * walk writes on the Map, not on a return value: 0x2FC54 clears Map.bFireHitResult
     * (0x1406C) at the top of every walk and 0x3010C sets it when the walked sample
     * leaves the map, and Map_TraceVoxelOccupancy stores its reason in Map.nFireArg5
     * (0x1404C) on the step that stopped the walk. Tact_ProcessFireAction reads both
     * back at the resolve (0x51CD4, 0x51F50), so a walk that does not publish them
     * leaves the resolve looking at the previous action's values. NULL keeps the two
     * values in ShotTrace only, which is how a test drives the trace. */
    u8  *pbFireHitResult;
    i32 *pnFireArg5;
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
    bool   bLeftMap;      /* the sample failed the bounds test (Map.bFireHitResult @ 0x1406C) */
    i32    nHitReason;    /* the reason the walk stopped, in Map.nFireArg5 @ 0x1404C */
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
 * Range budget inputs, all read straight off the records in the original. The soldier the ARM reads is
 * the SELECTED unit, not the acting one: `LDR R1,[R4,#0x15334]` at 0x31A54 is Map.pSelUnit, and the weight
 * is the thrown object's own (`LDR R2,[pItem]` at 0x31A4C, then `LDRB R2,[R2,#0xA5]`).
 *
 *   nStatStrength  Soldier.auStat[8] @ 0xB2 (LDRH [R1,#0xB2]) -- "Сила / Strength", the caption over stat 8
 *                  on the soldier readout (TEXTS 134), and the divisor Unit_RecalcInventoryStat's equip gate
 *                  divides the carried weight by.
 *   nStatStamina   Soldier.auStat[9] @ 0xB4 (LDRH [R1,#0xB4]) -- "Выносливость / Stamina", the caption over
 *                  stat 9 (TEXTS 129).
 *   nWeight        ObjectRecFull.bWeight @ 0xA5 (LDRB [R2,#0xA5])
 *   nStunOrArmor   Soldier.uStunOrArmor @ 0x8   (LDRH [R1,#8])
 *
 * The earlier names called auStat[8] "nStatThrow" and auStat[9] "nStatStrength", which contradicted both
 * the readout captions and the equip gate; pufo_unit.h's PUFO_STAT_STRENGTH / PUFO_STAT_STAMINA are the
 * same pair, and the arithmetic below is unchanged by the rename.
 */
typedef struct ThrowBudget {
    i32 nStatStrength;
    i32 nStatStamina;
    i32 nWeight;
    i32 nStunOrArmor;
} ThrowBudget;
#define PUFO_THROW_RANGE_SCALE   0.0625  /* 1/16, cell distance to range units */
#define PUFO_THROW_Z_DIV         8       /* integer divisor on the height gain */
#define PUFO_THROW_BUDGET_MIN    1.0
#define PUFO_THROW_BUDGET_MAX    22.0
#define PUFO_THROW_BUMP_BOUND    0.01
#define PUFO_THROW_BUMP_PLAIN    0.5
#define PUFO_THROW_Z_MAX         47.0
#define PUFO_THROW_LAND_MARGIN   8.0

/* The budget comes from the disassembly at 0x31a44..0x31a78:
 *
 *   MUL  R0, auStat[9], bWeight          ; w
 *   MUL  R1, auStat[8], uStunOrArmor     ; s
 *   BL   _rt_sdiv                        ; R0 = s/w, R1 = s%w
 *   MOVS R3, R0 / ADDMI R3, R3, #1      ; a negative quotient rounds toward zero
 *   MOV  R0, R3, ASR #1                 ; R0 = quotient >> 1
 *   BL   _itod                          ; int in R0 -> double
 *
 * An earlier reading of this block claimed _itod converts a 64-bit pair R1:R0, so
 * that the leftover remainder dominated the value and any non-zero division
 * remainder forced the 22.0 ceiling. That was wrong. The coredll helper set in
 * this binary is _itod, _utod, _stod, _itos, _stoi, _dtoi, _dtos, _dtou, _dtoui,
 * _addd, _subd, _muld, _divd, _led, _ltd, _ged, _gtd - and there is no _ltod, so
 * _itod is the 32-bit int -> double helper. Hex-Rays rendering it as
 * `itod(a1: sum, a2: stale)` is the same __PAIR64__ artifact that already misled
 * the throw-arc straddle reading.
 *
 * The rule is therefore plain, and a division remainder has no effect at all:
 *
 *   budget = clamp((double)((q + (q < 0 ? 1 : 0)) >> 1), 1.0, 22.0)
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

/* Scatter displacement triple, Map+0x15058 / 0x1505C / 0x15060 (unk_128A10,
 * unk_128A14, unk_128A18). Tact_ProcessMoveAction adds the three values to its
 * nX1/nY1/nZ1 target, and xrefs_to those globals finds no other reader. */
typedef struct ScatterOffset {
    i32 nDx;   /* 0x15058 */
    i32 nDy;   /* 0x1505C */
    i32 nDz;   /* 0x15060 */
} ScatterOffset;

/* Unit_RollChance @ 0x25e6c.
 *
 * A percent roll against nChance. On success all three scatter slots are
 * cleared and the function returns 0. On failure it fills the slots with a
 * displacement triple and returns the z component.
 *
 * Hex-Rays renders the base register as the Soldier argument. The ARM is not
 * ambiguous: both call sites in Tact_ProcessMoveAction pass R5, which is the
 * 0x1139B8 context base (see the LDR [R5,#(unk_113A00-0x1139B8)] at 0x50850),
 * and the body reads [R4,#0x14080] and writes [R4,#0x15058]. So the first
 * argument is the context, not the unit, and the port takes the two values the
 * function actually touches: the aim block bound and the scatter triple.
 *
 * The magnitude chain is exact:
 *   mag = (double)(((float)(dBound * 0.27) * (float)(nMiss + 10)) * 0.01f)
 * with 0.27 the double pair 0x3FD147AE:0x147AE148 and 0.01 the single
 * 0x3C23D70A. nMiss is Game_RandU16(roll - nChance), so the further the roll
 * misses, the wider the scatter.
 *
 * Each angle is Game_RandU16(628) divided by 100 with _rt_sdiv, which in this
 * binary takes the divisor in R0 and the dividend in R1 (see Throw_RangeBudget
 * and MapView_bResolveIsoCell @ 0x1b114). The division is integer, so the
 * angles are whole radians 0..6 and the remainder is dead.
 *
 * pScatter must not be NULL: the original always has the context block in
 * hand and stores all three slots on both paths. */
i32 Unit_RollChance(double dBound, i32 nChance, ScatterOffset *pScatter);

/* --- pieces of Tact_ProcessMoveAction @ 0x4f3f0, kept separate so each one is
 * testable on its own --- */

/* The muzzle offset for a shot, unk_A9768 @ 0xa9768. Index proven from ARM at
 * 0x50074..0x50094 with the clamp at 0x4FFD8..0x4FFE4:
 *     idx = pUnit->bFacing + 8 * (bSel + 2 * pUnit->nUnitType)
 * where bSel is ObjectRec.bListPri (+0xA4) clamped to 0 or 1. The caller adds the
 * three components to the cell centre (Map_CellCentreX/Y/Z) at 0x50098..0x500E0.
 *
 * Deviation: the original does not bound the index, so a unit type at or past
 * PUFO_UNIT_MUZZLE_TYPE_COUNT reads past the table. The port clamps the type and
 * the facing; byte_A7B80 has 24 rows while this table has 23 blocks. */
MuzzleOffset Tact_MuzzleOffset(const Soldier *pUnit, const ObjectRec *pObj);

/* 0x502CC..0x50328: charge one queued shot.
 *
 * The shot counter (pCtxBase dword at +0x2C, unk_1139E4, Hex-Rays nStatusDrawY)
 * is ALWAYS incremented, at 0x50320 after every other test.
 *
 * The ammo byte (TactItem.nAmmo @ +0x06) is decremented only when all of
 *   - pObj->bAmmoCap != 255 (255 means the weapon never runs out), AND
 *   - nAmmo != 0, AND
 *   - NOT (uSortKey is 13 or 35 while the shot counter is already non-zero)
 * holds. The counter test reads the value BEFORE the increment.
 *
 * The decrement itself is `nAmmo = (u8)(nAmmo + 0xFF)`, which is what the
 * original's `ADD R3, R2, #0xFF` / `STRB R3, [R0,#6]` does on a byte. */
void Tact_ChargeShotAmmo(TactItem *pWeapon, i32 *pnShotCount);

#endif /* PUFO_BALLIST_H */
