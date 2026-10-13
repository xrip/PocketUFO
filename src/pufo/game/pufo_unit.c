/* pufo_unit.c - soldier entities: identity, state predicates, placement, facing.
 *
 * Evidence: see pufo_unit.h for the full address list.
 */
#include "game/pufo_unit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game/pufo_los.h"
#include "audio/pufo_audio.h"    /* Sound_Play */
#include "util/pufo_str.h"       /* pufo_strcpy, pufo_strcat */
#include "util/pufo_rand.h"      /* Game_RandU16 */
#include "game/pufo_combat.h"   /* UnitRosters, CombatCtx, ShotHooks */
#include "game/pufo_roster.h"    /* SideLists, SideLists_RemoveUnit */
#include "game/pufo_statusmsg.h" /* StatusStack, StatusHooks, StatusMsg_Push */
#include "game/pufo_vistry.h"   /* VisRay, ray-table counts */
#include "game/pufo_tactctl.h"   /* mission type ids */
#include "game/pufo_ballist.h"   /* AimState, Tact_TraceProjectileArc */
#include "game/pufo_turn.h"        /* TactCtx272 */
#include "game/pufo_boot.h"   /* the extracted name tables, and TextTables_* */
#include "game/pufo_font.h"   /* Font_GetSprByIndex / g_fontSys */
#include "game/pufo_path.h"   /* g_aStepDir */
#include "util/pufo_rand.h"   /* Game_RandU16 */
#include "util/pufo_str.h"    /* pufo_strncpy / pufo_strcat */

/* byte_A7B80 @ 0xa7b80, stride 4, 24 rows. Dumped from the binary. */
const u8 g_aUnitTypeIsLarge[PUFO_UNIT_TYPE_COUNT] = {
    0, 0, 0, 0, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 1, 1, 0, 0
};
const u8 g_aUnitTypeFlag0[PUFO_UNIT_TYPE_COUNT] = {
    0, 0, 0, 1, 0, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0
};

/* unk_A9768 @ 0xa9768, 23 x 2 x 8 MuzzleOffset. Dumped from the binary. */
const MuzzleOffset g_aUnitMuzzleOffset[PUFO_UNIT_MUZZLE_TYPE_COUNT][2][8] = {
    /* type  0 */ { { {5,-5,3}, {6,-6,2}, {3,0,0}, {-1,1,0}, {-4,4,1}, {-7,7,3}, {-3,3,4}, {2,-2,4} },
                   { {6,-6,4}, {7,-7,2}, {4,-4,-2}, {-1,1,0}, {-6,6,0}, {-7,7,3}, {-4,4,4}, {1,-1,4} } },
    /* type  1 */ { { {5,-5,3}, {6,-6,2}, {3,0,0}, {-1,1,0}, {-4,4,1}, {-7,7,3}, {-3,3,4}, {2,-2,4} },
                   { {6,-6,4}, {7,-7,2}, {4,-4,-2}, {-1,1,0}, {-6,6,0}, {-7,7,3}, {-4,4,4}, {1,-1,4} } },
    /* type  2 */ { { {5,-5,3}, {6,-6,2}, {3,0,0}, {-1,1,0}, {-4,4,1}, {-7,7,3}, {-3,3,4}, {2,-2,4} },
                   { {6,-6,4}, {7,-7,2}, {4,-4,-2}, {-1,1,0}, {-6,6,0}, {-7,7,3}, {-4,4,4}, {1,-1,4} } },
    /* type  3 */ { { {5,-5,3}, {6,-6,2}, {3,0,0}, {-1,1,0}, {-4,4,1}, {-7,7,3}, {-3,3,4}, {2,-2,4} },
                   { {6,-6,4}, {7,-7,2}, {4,-4,-2}, {-1,1,0}, {-6,6,0}, {-7,7,3}, {-4,4,4}, {1,-1,4} } },
    /* type  4 */ { { {5,-5,-1}, {3,-3,-5}, {3,-4,-5}, {0,0,-4}, {-3,3,-4}, {-3,3,-5}, {-5,5,-2}, {0,0,0} },
                   { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    /* type  5 */ { { {5,-5,-1}, {3,-3,-5}, {3,-4,-5}, {0,0,-4}, {-3,3,-4}, {-3,3,-5}, {-5,5,-2}, {0,0,0} },
                   { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    /* type  6 */ { { {5,-5,-1}, {3,-3,-5}, {3,-4,-5}, {0,0,-4}, {-3,3,-4}, {-3,3,-5}, {-5,5,-2}, {0,0,0} },
                   { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    /* type  7 */ { { {5,-5,-1}, {3,-3,-5}, {3,-4,-5}, {0,0,-4}, {-3,3,-4}, {-3,3,-5}, {-5,5,-2}, {0,0,0} },
                   { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    /* type  8 */ { { {5,-5,-1}, {3,-3,-5}, {3,-4,-5}, {0,0,-4}, {-3,3,-4}, {-3,3,-5}, {-5,5,-2}, {0,0,0} },
                   { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    /* type  9 */ { { {5,-5,0}, {5,-5,-1}, {3,0,-2}, {-3,3,-2}, {-6,6,-2}, {-5,5,0}, {-3,3,4}, {3,-3,2} },
                   { {7,-7,2}, {7,-7,0}, {4,-4,-3}, {-2,2,-2}, {-6,6,-2}, {-7,7,0}, {-6,6,4}, {2,-2,5} } },
    /* type 10 */ { { {5,-5,3}, {6,-6,2}, {3,0,0}, {-1,1,0}, {-4,4,1}, {-7,7,3}, {-3,3,4}, {2,-2,4} },
                   { {6,-6,4}, {7,-7,2}, {4,-4,-2}, {-1,1,0}, {-6,6,0}, {-7,7,3}, {-4,4,4}, {1,-1,4} } },
    /* type 11 */ { { {5,-5,3}, {6,-6,2}, {3,0,0}, {-1,1,0}, {-4,4,1}, {-7,7,3}, {-3,3,4}, {2,-2,4} },
                   { {6,-6,4}, {7,-7,2}, {4,-4,-2}, {-1,1,0}, {-6,6,0}, {-7,7,3}, {-4,4,4}, {1,-1,4} } },
    /* type 12 */ { { {5,-5,3}, {6,-6,2}, {3,0,0}, {-1,1,0}, {-4,4,1}, {-7,7,3}, {-3,3,4}, {2,-2,4} },
                   { {6,-6,4}, {7,-7,2}, {4,-4,-2}, {-1,1,0}, {-6,6,0}, {-7,7,3}, {-4,4,4}, {1,-1,4} } },
    /* type 13 */ { { {5,-5,3}, {6,-6,2}, {3,0,0}, {-1,1,0}, {-4,4,1}, {-7,7,3}, {-3,3,4}, {2,-2,4} },
                   { {6,-6,4}, {7,-7,2}, {4,-4,-2}, {-1,1,0}, {-6,6,0}, {-7,7,3}, {-4,4,4}, {1,-1,4} } },
    /* type 14 */ { { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} },
                   { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    /* type 15 */ { { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} },
                   { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    /* type 16 */ { { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} },
                   { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    /* type 17 */ { { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} },
                   { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    /* type 18 */ { { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} },
                   { {0,0,-3}, {0,0,-3}, {0,0,-3}, {0,0,-3}, {0,0,-3}, {0,0,-3}, {0,0,-3}, {0,0,-3} } },
    /* type 19 */ { { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} },
                   { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    /* type 20 */ { { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} },
                   { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    /* type 21 */ { { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} },
                   { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
    /* type 22 */ { { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} },
                   { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} } },
};

/* Soldier_ctor @ 0x476c8. The original clears exactly one dword (the tail link
 * pLinked1040 @ 0x414); everything else is set by the caller. The port zeroes
 * the modelled prefix and the tail, which is a superset of that and cannot
 * differ observably because the tail's only named field is the link. */
void Unit_ctor(Soldier *pUnit)
{
    memset(pUnit, 0, sizeof(*pUnit));
}

bool Unit_bIsOperative(const Soldier *pUnit)
{
    return pUnit->bStatus == UNIT_STATUS_OPERATIVE;
}

bool Unit_bIsLarge(const Soldier *pUnit)
{
    if (pUnit->nUnitType >= PUFO_UNIT_TYPE_COUNT) {
        return false;
    }
    return g_aUnitTypeIsLarge[pUnit->nUnitType] == 1;
}

/* Unit_bFacingAngleDuel @ 0x4dcf4. Which of two units is better turned towards
 * the other, used by the opportunity-fire decision.
 *
 * Each side gets an absolute angle error, then a random reaction roll scaled by
 * that error and by a luck roll, and the larger final value wins.
 *
 * The angle constants are the game's own approximations, read from the double
 * pairs it builds: a facing step of 0.785375 (not pi/4), a full turn of 6.283
 * (not 2*pi), and a half turn of 3.1415. Both the facing angle and the bearing
 * are normalised into [0, 2*pi) by adding the full turn when they come out
 * negative, and the error is wrapped into [0, pi] the same way.
 *
 * The original's unused first context argument is dropped. */
#define PUFO_FACING_STEP  0.785375
#define PUFO_TURN_FULL    6.283
#define PUFO_TURN_HALF    3.1415
#define PUFO_FACING_TOL_1 0.19634375 /* PUFO_FACING_STEP / 4 */
#define PUFO_FACING_TOL_2 0.3926875  /* PUFO_FACING_STEP / 2 */

static double Unit_FacingAngle(const Soldier *pUnit)
{
    double dAngle = (double)((i32)pUnit->bFacing - 2) * PUFO_FACING_STEP;

    if (dAngle < 0.0) {
        dAngle += PUFO_TURN_FULL;
    }
    return dAngle;
}

static double Unit_CellBearing(const MapCell *pFrom, const MapCell *pTo)
{
    double dBearing = atan2((double)(pTo->nY - pFrom->nY),
                            (double)(pTo->nX - pFrom->nX));

    if (dBearing < 0.0) {
        dBearing += PUFO_TURN_FULL;
    }
    return dBearing;
}

/* The error is truncated to an integer before its magnitude is taken, exactly as
 * the original's dtoi/abs32 pair does. */
static double Unit_AngleError(double dAngle, double dBearing)
{
    double dError = (double)(i32)(dAngle - dBearing);

    if (dError < 0.0) {
        dError = -dError;
    }
    if (dError > PUFO_TURN_HALF) {
        dError = PUFO_TURN_FULL - dError;
    }
    return dError;
}

static i32 Unit_AngleScore(double dError)
{
    if (dError <= PUFO_FACING_TOL_1) {
        return 2;
    }
    return dError <= PUFO_FACING_TOL_2 ? 1 : 0;
}

bool Unit_bFacingAngleDuel(const Soldier *pUnitA, const Soldier *pUnitB)
{
    double dErrA = Unit_AngleError(Unit_FacingAngle(pUnitA),
                                   Unit_CellBearing(pUnitA->pCell, pUnitB->pCell));
    double dErrB = Unit_AngleError(Unit_FacingAngle(pUnitB),
                                   Unit_CellBearing(pUnitB->pCell, pUnitA->pCell));
    i32 nScoreA = Unit_AngleScore(dErrA);
    i32 nScoreB = Unit_AngleScore(dErrB);

    i32 nRollA = Game_RandU16(0x64);
    if ((i32)pUnitA->auStat[3] < nRollA) {
        nRollA = 0;
    }
    i32 nRollB = Game_RandU16(0x64);
    if ((i32)pUnitB->auStat[3] < nRollB) {
        nRollB = 0;
    }
    i32 nLuckA = Game_RandU16(0x14);
    i32 nLuckB = Game_RandU16(0x14);

    if (nLuckA <= nLuckB) {
        nRollB = ((nLuckB - nLuckA) / 5 * (nRollB / 2) + nRollB) * nScoreB;
    } else {
        nRollA = ((nLuckA - nLuckB) / 5 * (nRollA / 2) + nRollA) * nScoreA;
    }
    return nRollA > nRollB;
}

/* Unit_GetExtraSpr @ 0x2e59c. The disassembly reads the low byte of
 * anim.uFacingResetH (LDRB [pUnit,#0x32]) and the u16 anim.uExtraSprBase
 * (LDRH [pUnit,#0x26]), then indexes anMc1Base[uExtraSprBase] (the u16 at
 * g_fontSys+0x3a) and adds the facing byte. */
Mc1Rec *Rec_GetExtraSpr(u16 nBaseIdx, u8 nFacingByte)
{
    if (nFacingByte == 0) {
        return NULL;
    }
    return Font_GetSprByIndex(&g_fontSys,
        (u16)(g_fontSys.anMc1Base[nBaseIdx] + nFacingByte));
}

Mc1Rec *Unit_GetExtraSpr(const Soldier *pUnit)
{
    return Rec_GetExtraSpr(pUnit->multiOrAnim.anim.uExtraSprBase,
                           (u8)pUnit->multiOrAnim.anim.uFacingResetH);
}

/* The delete, compact and decrement half of Soldier_RemoveById @ 0x36b8c. It is also used
 * directly where the caller already knows the index. Both memory-safety deviations from the
 * original are documented in the header. */
i32 Unit_RemoveAt(Soldier **apSoldiers, i32 nCount, i32 nIndex)
{
    if (nCount <= 0) {
        return nCount;
    }
    if (nIndex < 0 || nIndex >= nCount) {
        return nCount;
    }
    free(apSoldiers[nIndex]);
    for (i32 i = nIndex + 1; i < nCount; ++i) {
        apSoldiers[i - 1] = apSoldiers[i];
    }
    return nCount - 1;
}

/* Soldier_RemoveById @ 0x36b8c. Find the first soldier whose roster id (nUnk440) matches, then
 * delete, compact and decrement -- which is Unit_RemoveAt, so this is the search half on top of
 * it.
 *
 * The original has no "was it found" test. Its search loop exits when the index reaches the
 * count, and 0x36BE0 then deletes apSoldiers[that index]: when nothing matched that is one slot
 * past the array, and the count is decremented anyway. The port returns the list untouched.
 *
 * Deviation: the original also deletes a NULL slot without complaint, and reads the count as a
 * u16, so a count of 0 wraps to 65535 on the way out. The port keeps the count in an int. */
i32 Soldier_RemoveById(Soldier **apSoldiers, i32 nCount, i32 nId)
{
    if (apSoldiers == NULL || nCount <= 0) {
        return nCount; /* 0x36BB0 */
    }

    i32 nIndex = 0;
    while (nIndex < nCount) {
        if (apSoldiers[nIndex] != NULL && apSoldiers[nIndex]->nUnk440 == nId) {
            break; /* 0x36BC4 */
        }
        ++nIndex; /* 0x36BC8 */
    }

    if (nIndex >= nCount) {
        return nCount; /* deviation: the original deletes apSoldiers[nCount] here */
    }

    return Unit_RemoveAt(apSoldiers, nCount, nIndex); /* 0x36BE0..0x36C4C */
}

/* Armour granted purely by unit type, from Unit_ResetBattleState @ 0x498b0. */
i32 Unit_ArmorForType(u16 nUnitType)
{
    if (nUnitType == PUFO_UNITTYPE_ARMOR_100) {
        return 100;
    }
    if (nUnitType == PUFO_UNITTYPE_ARMOR_150) {
        return 150;
    }
    return 0;
}

/* Unit_ResetBattleState @ 0x498b0.
 *
 * Field order copied from the decompilation. Note two things that look wrong and
 * are not: the TU/HP/stun values are pulled from auStat with auStat[9] feeding
 * uStunOrArmor, and the humanoid flag is first set to 1 and then cleared for
 * every type <= 8 -- so it ends up set only for types 9 and above. */
void Unit_ResetBattleState(Soldier *pUnit, u8 bFacing, u16 nUnitType,
                           const u16 *auStat, i32 nStatCount)
{
    pUnit->bFacing   = bFacing;
    pUnit->nDrawOffX = 0;
    pUnit->nDrawOffY = 0;
    pUnit->bFlags    = (u8)(pUnit->bFlags & 0xC0);
    pUnit->pCell     = NULL;
    pUnit->nAnimPose = 1;

    pUnit->bMorale      = 100;
    pUnit->uTuSpent     = 0;
    pUnit->nMultiTarget = 0;
    pUnit->nUnitType    = (u8)nUnitType;

    if (auStat != NULL && nStatCount > 0) {
        pUnit->uTu = auStat[0];
    }
    if (auStat != NULL && nStatCount > 1) {
        pUnit->uHp = auStat[1];
    }
    if (auStat != NULL && nStatCount > 9) {
        pUnit->uStunOrArmor = auStat[9];
    }

    /* bHumanoidInv is set to 1 and then cleared for types <= 8, which leaves it
     * set only for types 9+. Reproduced literally. */
    pUnit->bUnk148 = 0;
}

/* Unit_FacingToward @ 0x49bf0.
 *
 * Source form of the original's arithmetic:
 *     d = atan2(y0 - y1, x0 - x1)
 *     d -= 1.57075                            ; truncated pi/2, NOT pi/2
 *     if (d < 0) d += 6.283                    ; truncated 2*pi
 *     d += 0.3926875                           ; truncated pi/8
 *     facing = (int)(d * 1.273277096928219)    ; == 8 / 6.283
 *     if (facing < 0) facing += 8
 *     if (facing > 7) facing -= 8
 *
 * The truncated constants are kept verbatim so the sector boundaries match the
 * original's. Substituting the exact pi values would move them. */
u8 Unit_FacingToward(i32 nFromX, i32 nFromY, i32 nToX, i32 nToY)
{
    double d;
    int    nFacing;

    if (nToX == nFromX && nToY == nFromY) {
        return 0;
    }

    d = atan2((double)(nFromY - nToY), (double)(nFromX - nToX));
    d -= PUFO_FACING_SUB;
    if (d < 0.0) {
        d += PUFO_FACING_WRAP;
    }
    d += PUFO_FACING_ADD;
    nFacing = (int)(d * PUFO_FACING_SCALE);
    if (nFacing < 0) {
        nFacing += PUFO_FACING_COUNT;
    }
    if (nFacing >= PUFO_FACING_COUNT) {
        nFacing -= PUFO_FACING_COUNT;
    }
    return (u8)nFacing;
}

/* Unit_PlaceOnMap @ 0x49a0c.
 *
 * A large unit claims a 2x2 block based at (x, y) and releases the same four on
 * the way out. The cell lookup is injected so this is testable without the full
 * game context. */
void Unit_PlaceOnMap(Soldier *pUnit, i32 nX, i32 nY, i32 nLayer,
                     UnitCellLookup pfnLookup, void *pUser)
{
    MapCell *pNew;

    if (pUnit->pCell != NULL) {
        if (Unit_bIsLarge(pUnit)) {
            MapCell *pOld = pUnit->pCell;

            MapCell_SetOccupant(pfnLookup(pOld->nX + 1, pOld->nY, pOld->nZ, pUser), NULL);
            MapCell_SetOccupant(pfnLookup(pOld->nX, pOld->nY + 1, pOld->nZ, pUser), NULL);
            MapCell_SetOccupant(pfnLookup(pOld->nX + 1, pOld->nY + 1, pOld->nZ, pUser), NULL);
        }
        MapCell_SetOccupant(pUnit->pCell, NULL);
    }

    pNew = pfnLookup(nX, nY, nLayer, pUser);
    if (pNew == NULL) {
        pUnit->pCell = NULL;
        return;
    }
    pUnit->pCell = pNew;
    if (Unit_bIsLarge(pUnit)) {
        MapCell *pNeighbour;

        pNeighbour = pfnLookup(nX + 1, nY, nLayer, pUser);
        if (pNeighbour != NULL) {
            MapCell_SetOccupant(pNeighbour, pUnit);
        }
        pNeighbour = pfnLookup(nX, nY + 1, nLayer, pUser);
        if (pNeighbour != NULL) {
            MapCell_SetOccupant(pNeighbour, pUnit);
        }
        pNeighbour = pfnLookup(nX + 1, nY + 1, nLayer, pUser);
        if (pNeighbour != NULL) {
            MapCell_SetOccupant(pNeighbour, pUnit);
        }
    }
    MapCell_SetOccupant(pNew, pUnit);
}

/* --- Soldier_Generate @ 0x49410 --- */

/* First-name index ranges, from the two draws in Soldier_Generate:
 *   male:   Game_RandU16(0x46)        -> 0..69
 *   female: Game_RandU16(0x12) + 70   -> 70..87 */
#define PUFO_FIRSTNAME_MALE_MAX   0x46u
#define PUFO_FIRSTNAME_FEMALE_MAX 0x12u
#define PUFO_FIRSTNAME_FEMALE_BASE 70u

/* Last-name draws:
 *   male:   Game_RandU16(0x40)        -> 0..63
 *   female: Game_RandU16(0x18); when the draw is >= 17 the entry is +47, i.e.
 *           64..70, otherwise the draw is used unchanged (0..16).
 * The second branch reusing the male range is a genuine quirk of the original:
 * a female soldier whose draw lands below 17 gets a male-surname slot. Kept. */
#define PUFO_LASTNAME_MALE_MAX   0x40u
#define PUFO_LASTNAME_FEMALE_MAX 0x18u
#define PUFO_LASTNAME_FEMALE_THRESHOLD 17u
#define PUFO_LASTNAME_FEMALE_OFFSET    47u

void Unit_GenerateName(Soldier *pUnit)
{
    /* The order of these two draws is the RNG contract; do not reorder. */
    if (pUnit->bFemale != 0) {
        u8 nFirst = (u8)Game_RandU16(PUFO_FIRSTNAME_FEMALE_MAX);
        u8 nLast  = (u8)Game_RandU16(PUFO_LASTNAME_FEMALE_MAX);
        u8 nLastIdx;

        pufo_strncpy(pUnit->szName, TextTables_FirstName((u8)(nFirst + PUFO_FIRSTNAME_FEMALE_BASE)),
                     sizeof(pUnit->szName));
        if (nLast >= PUFO_LASTNAME_FEMALE_THRESHOLD) {
            nLastIdx = (u8)(nLast + PUFO_LASTNAME_FEMALE_OFFSET);
        } else {
            nLastIdx = nLast;
        }
        (void)pufo_strcat(pUnit->szName, " ", sizeof(pUnit->szName));
        (void)pufo_strcat(pUnit->szName, TextTables_LastName(nLastIdx), sizeof(pUnit->szName));
    } else {
        u8 nFirst = (u8)Game_RandU16(PUFO_FIRSTNAME_MALE_MAX);
        u8 nLast  = (u8)Game_RandU16(PUFO_LASTNAME_MALE_MAX);

        pufo_strncpy(pUnit->szName, TextTables_FirstName(nFirst), sizeof(pUnit->szName));
        (void)pufo_strcat(pUnit->szName, " ", sizeof(pUnit->szName));
        (void)pufo_strcat(pUnit->szName, TextTables_LastName(nLast), sizeof(pUnit->szName));
    }
}

void Unit_Generate(Soldier *pUnit, u8 nKind)
{
    u16 uSeventh;

    pUnit->nUnitType   = 0;
    /* bFemale is an int in the original (a dword at 0x9C), and the draw is
     * compared against 0, so roughly one in three soldiers is female. */
    pUnit->bFemale     = (Game_RandU16(3u) == 0) ? 1 : 0;
    pUnit->nLook       = (i32)Game_RandU16(4u);
    pUnit->bUnk183     = 0xFF;
    pUnit->nSpottedCount = 0;
    pUnit->nMultiTarget  = 0;
    pUnit->bUnk149     = 0;
    pUnit->nKind       = nKind;
    pUnit->bRankType   = 0;
    pUnit->uMissions   = 0;
    pUnit->bKneelOrPrimed = 0;
    pUnit->uKills      = 0;
    pUnit->bUnk435     = 0;
    pUnit->nUnk436     = 0;

    /* The original wraps this in a retry loop whose collision test compares two
     * addresses and can never fire. See the header comment: the loop is dead
     * code, and generating the name once keeps the RNG draw count identical. */
    Unit_GenerateName(pUnit);

    /* Stat rolls. The destination order is deliberately non-sequential (index 9
     * before 1, 8 before 6) and must be preserved: it is the RNG draw order. */
    pUnit->auStat[0] = (u16)(Game_RandU16(0x0Au) + 50u);
    pUnit->auStat[9] = (u16)(Game_RandU16(0x1Eu) + 40u);
    pUnit->auStat[1] = (u16)(Game_RandU16(0x14u) + 40u);
    pUnit->auStat[2] = (u16)(Game_RandU16(0x32u) + 10u);
    pUnit->auStat[3] = (u16)(Game_RandU16(0x1Eu) + 30u);
    pUnit->auStat[4] = (u16)(Game_RandU16(0x1Eu) + 40u);
    pUnit->auStat[5] = (u16)(Game_RandU16(0x14u) + 30u);
    pUnit->auStat[8] = (u16)(Game_RandU16(0x3Cu) + 20u);
    pUnit->auStat[6] = (u16)(Game_RandU16(0x14u) + 10u);
    uSeventh         = (u16)(Game_RandU16(0x64u) + 1u);

    pUnit->uTuSpent   = 0;
    pUnit->bUnk248    = 0;
    pUnit->auStat[7]  = uSeventh;
    pUnit->uHp        = pUnit->auStat[1];
    pUnit->bUnk422    = 0;
    pUnit->bUnk423    = 0;
    pUnit->bFlags     = (u8)(pUnit->bFlags & ~0x20u);
    pUnit->bHumanoidInv = 1;
    pUnit->abReady252[0] = 1;
    pUnit->abReady252[1] = 1;
    pUnit->abReady252[2] = 1;
    pUnit->abReady252[3] = 1;
    pUnit->abReady252[4] = 1;

    /* The base-stat copy is offset by one at the tail: base[7] takes stat[8] and
     * base[8] takes stat[9]. Reproduced; it is not an off-by-one in the port. */
    pUnit->auStatBase[0] = pUnit->auStat[0];
    pUnit->auStatBase[1] = pUnit->auStat[1];
    pUnit->auStatBase[2] = pUnit->auStat[2];
    pUnit->auStatBase[3] = pUnit->auStat[3];
    pUnit->auStatBase[4] = pUnit->auStat[4];
    pUnit->auStatBase[5] = pUnit->auStat[5];
    pUnit->auStatBase[6] = pUnit->auStat[6];
    pUnit->auStatBase[7] = pUnit->auStat[8];
    pUnit->auStatBase[8] = pUnit->auStat[9];

    pUnit->bUnk961 = 0;
    for (i32 i = 0; i < 25; ++i) {
        pUnit->auUnk962[i]   = 0;
        pUnit->abUnk1012[i]  = 0;
    }
    pUnit->pLinked1040 = NULL;
}

static i32 Unit_i32Abs(i32 nValue)
{
    return nValue < 0 ? -nValue : nValue;
}

/* The classification half of Soldier_CreateAndClassify @ 0x22ba0, split out because the gates are the
 * interesting part and are otherwise unreachable in a test: inside the original they run straight after a
 * fresh stat roll, so no caller can aim them at chosen stats. */
i32 Unit_ClassFromStats(const Soldier *pUnit)
{
    i32 nAvgA = ((i32)pUnit->auStat[8] + pUnit->auStat[9] + pUnit->auStat[5] + pUnit->auStat[0]) >> 2;
    i32 nAvgB = ((i32)pUnit->auStat[3] + pUnit->auStat[4]) / 2;
    i32 nAvgC = ((i32)pUnit->auStat[6] + pUnit->auStat[7] + pUnit->auStat[2]) / 3;
    i32 nClass = -1;
    bool bBalanced = true;                  /* v4 = 1 at 0x22BB4 */

    if (nAvgA - nAvgB > 5 && nAvgA - nAvgC > 5) {
        bBalanced = false;
        nClass = 0;
    }
    if (nAvgB - nAvgC > 7 && nAvgB - nAvgA > 5) {
        bBalanced = false;
        nClass = 1;
    }
    if (nAvgC - nAvgB > 0 && nAvgC - nAvgA > 0) {
        bBalanced = false;
        nClass = 2;
    }

    if (bBalanced) {
        if (Unit_i32Abs(nAvgA - nAvgB) < 10) {
            nClass = 3;                     /* 0x22D40 */
        }
        if (Unit_i32Abs(nAvgB - nAvgC) < 10 && nAvgB > nAvgA) {
            nClass = 4;                     /* 0x22D74 */
        }
        if (Unit_i32Abs(nAvgA - nAvgC) < 10 && nAvgA > nAvgB) {
            nClass = 5;                     /* 0x22DAC */
        }
    }

    if (nClass == -1) {
        nClass = (i32)Game_RandU16(3) + 3;  /* 0x22DE8: 3, 4 or 5 */
    }
    return nClass;
}

/* Soldier_CreateAndClassify @ 0x22ba0.
 *
 * Allocates a soldier, generates it, classifies it from three averages of its own stats, hands out the next
 * roster id, and adds one to the roster count at the very end.
 *
 * ## The three averages, and what the classification actually measures
 *
 *   nAvgA = (auStat[8] + auStat[9] + auStat[5] + auStat[0]) >> 2
 *   nAvgB = (auStat[3] + auStat[4]) / 2
 *   nAvgC = (auStat[6] + auStat[7] + auStat[2]) / 3
 *
 * Note the first uses a shift and the others a divide. For non-negative sums they agree, and the stats are
 * all positive here, so it makes no difference - but it is what the original does.
 *
 * Hex-Rays renders each gate as a bool that is conditionally recomputed, which hides a plain OR:
 *
 *   v12 = (A - B <= 5);  if (A - B > 5) v12 = (A - C <= 5);
 *
 * so `!v12` means `A - B > 5 AND A - C > 5`. Read that way the three gates are simply "one average dominates
 * the other two", and the classes are 0 = A dominates, 1 = B dominates, 2 = C dominates:
 *
 *   class 0: A - B > 5  and A - C > 5
 *   class 1: B - C > 7  and B - A > 5
 *   class 2: C - B > 0  and C - A > 0
 *
 * If none of them fired, the soldier is balanced and gets 3, 4 or 5 from closeness:
 *
 *   class 3: |A - B| < 10
 *   class 4: |B - C| < 10 and B > A
 *   class 5: |A - C| < 10 and A > B
 *
 * and 4 and 5 are mutually exclusive because one needs B > A and the other A > B. If still unclassified -
 * which needs A - B >= 10, and either |B - C| >= 10 or B <= A, and either |A - C| >= 10 or A <= B - the
 * original draws Game_RandU16(3) + 3, so 3, 4 or 5.
 *
 * ## Two details worth naming
 *
 * The roster id is assigned BEFORE the counter is bumped: the soldier gets unk_186554 as it stands, and the
 * increment happens at 0x22DF0. The return value is that incremented counter, not the id the soldier got,
 * so callers receive the NEXT id. Reproduced.
 *
 * The count is a u16 in the original - apSoldiers[LOWORD(nSoldierCount)] and ++*(u16 *)&nSoldierCount - and
 * there is no bounds check on the roster array at all.
 *
 * Deviation, recorded in docs/parity/bugs.tsv: the original has no check after operator new. It stores the
 * null into apSoldiers and then dereferences it on the very next instruction at 0x22C10, so a failed
 * allocation is a crash rather than a behaviour. The port returns without touching the roster instead. */
i32 Unit_CreateAndClassify(UnitCreateEnv *pEnv, u8 nKind)
{
    Soldier *pNew;
    i32 nSlot;
    i32 nId;

    pNew = (Soldier *)malloc(sizeof(Soldier));
    if (pNew == NULL) {
        return *pEnv->pnNextSoldierId;
    }
    Unit_ctor(pNew);

    nSlot = (i32)(u16)*pEnv->pnSoldierCount;
    pEnv->apSoldiers[nSlot] = pNew;

    nId = *pEnv->pnNextSoldierId;
    pNew->nUnk440 = nId;                    /* 0x22C10 */
    Unit_Generate(pNew, nKind);             /* 0x22C20 */

    pNew->nClass = Unit_ClassFromStats(pNew);

    *pEnv->pnNextSoldierId = nId + 1;               /* 0x22DF0 */
    *pEnv->pnSoldierCount = (i32)(u16)(*pEnv->pnSoldierCount + 1);   /* 0x22E00, a u16 increment */
    return nId + 1;                                 /* the incremented counter, not the id handed out */
}

/* --- Tact_UpdateUnitControl (0x4a444), the unit-arithmetic half --- */

/* Morale threshold: at or above 95 the unit is fully rallied and snaps to 100,
 * otherwise morale rises by 5. Verified from the decompilation's `>= 0x5F`. */
#define PUFO_MORALE_RALLY_THRESHOLD 95
#define PUFO_MORALE_RALLY_MAX       100
#define PUFO_MORALE_STEP            5

/* TU costs, from the same function. The `+3` branch fires when the cell's low
 * 3-bit field is non-zero -- the field GameTick_Step @ 0x27324 decrements. */
#define PUFO_RECOVER_TU_PENALTY 3
#define PUFO_RECOVER_TU_RELIEF  10

bool Unit_ApplyControlRecovery(Soldier *pUnit)
{
    u8 bKneel;

    /* Idempotence guard: the original returns immediately once flagged. */
    if (pUnit->bControlRecovered != 0) {
        return false;
    }

    /* Knockout bookkeeping. bKneelOrPrimed is the pending damage; if it exceeds
     * current HP the unit is dropped to 0 rather than going negative. */
    bKneel = pUnit->bKneelOrPrimed;
    if (pUnit->uHp < (u16)bKneel) {
        pUnit->uHp = 0;
    } else {
        pUnit->uHp = (u16)(pUnit->uHp - (u16)bKneel);
    }
    pUnit->bUnk1037 = 0;

    /* TU adjustment, SMALL UNITS ONLY. Large units keep their spent TU. */
    if (!Unit_bIsLarge(pUnit)) {
        if (pUnit->pCell != NULL && (pUnit->pCell->nFlags32 & 7u) != 0) {
            pUnit->uTuSpent = (u16)(pUnit->uTuSpent + PUFO_RECOVER_TU_PENALTY);
        } else if (pUnit->uTuSpent >= PUFO_RECOVER_TU_RELIEF) {
            pUnit->uTuSpent = (u16)(pUnit->uTuSpent - PUFO_RECOVER_TU_RELIEF);
        } else {
            pUnit->uTuSpent = 0;
        }
    }

    /* Morale. */
    if (pUnit->bMorale >= PUFO_MORALE_RALLY_THRESHOLD) {
        pUnit->bMorale = PUFO_MORALE_RALLY_MAX;
    } else {
        pUnit->bMorale = (u8)(pUnit->bMorale + PUFO_MORALE_STEP);
    }

    pUnit->bControlRecovered = 1;
    return true;
}

/* Unit_UpdateAnimPose @ 0x4978c. */
void Unit_UpdateAnimPose(Soldier *pUnit, const AnimHandInfo *pHandL,
                         const AnimHandInfo *pHandR)
{
    /* A hand counts as a weapon (poses +10) when its object has list priority 1
     * or sort key 22/24; any other held item gives +5. */
    bool bHasHandL;
    bool bHasHandR;

    if ((pUnit->bFlags & 0x20) != 0) {
        pUnit->bFlags &= 0xE0u;
        pUnit->nAnimPose = 18;
        return;
    }
    if ((pUnit->bFlags & 4) != 0) {
        /* Aiming: 15 when kneeling, 17 when climbing/alt, else 16. */
        if ((pUnit->bFlags & 1) != 0) {
            pUnit->nAnimPose = 15;
        } else if ((pUnit->bFlags & 0x10) != 0) {
            pUnit->nAnimPose = 17;
        } else {
            pUnit->nAnimPose = 16;
        }
        return;
    }

    /* Not aiming: pick the base pose from the walk/kneel/climb flags. */
    if ((pUnit->bFlags & 1) != 0) {
        pUnit->nAnimPose = 0;
    } else if ((pUnit->bFlags & 0x10) != 0 || (pUnit->bFlags & 2) == 0) {
        if ((pUnit->bFlags & 0x10) != 0) {
            pUnit->nAnimPose = ((pUnit->bFlags & 2) != 0) ? 4 : 2;
        } else {
            pUnit->nAnimPose = 1;
        }
    } else {
        pUnit->nAnimPose = 3;
    }

    bHasHandL = pHandL != NULL && pHandL->bPresent;
    bHasHandR = pHandR != NULL && pHandR->bPresent;
    if (bHasHandL && (pHandL->bListPri == 1 || pHandL->uSortKey == 22
                      || pHandL->uSortKey == 24)) {
        pUnit->nAnimPose += 10;
        return;
    }
    if (bHasHandR && (pHandR->bListPri == 1 || pHandR->uSortKey == 22
                      || pHandR->uSortKey == 24)) {
        pUnit->nAnimPose += 10;
        return;
    }
    if (!bHasHandL && !bHasHandR) {
        return;
    }
    pUnit->nAnimPose += 5;
}

/* Unit_CanStepExitDir @ 0x36d68. The 8 x 4 (dx, dy, kind) wall-probe triples,
 * transcribed from the 96-dword table the decompiler expanded. A leading dx of 5
 * is the padding that stops the scan. */
static const i8 s_aExitProbe[8][4][3] = {
    { { 0,  0, 1 }, { 5,  5, 5 }, { 5,  5, 5 }, { 5,  5, 5 } },
    { { 0,  0, 1 }, { 1,  0, 1 }, { 1,  0, 2 }, { 1, -1, 2 } },
    { { 1,  0, 2 }, { 5,  5, 5 }, { 5,  5, 5 }, { 5,  5, 5 } },
    { { 0, -1, 1 }, { 1,  0, 2 }, { 1,  1, 1 }, { 1,  1, 2 } },
    { { 0, -1, 1 }, { 5,  5, 5 }, { 5,  5, 5 }, { 5,  5, 5 } },
    { { 0,  0, 2 }, { -1, 1, 1 }, { 0, -1, 1 }, { 0, -1, 2 } },
    { { 0,  0, 2 }, { 5,  5, 5 }, { 5,  5, 5 }, { 5,  5, 5 } },
    { { 0,  0, 2 }, { 0,  0, 1 }, { -1, 0, 1 }, { 0, -1, 2 } },
};

bool Unit_CanStepExitDir(const MapCell *pOrigin, i32 nDir, const MapGeom *pGeom,
                         MapCellLookup pfnLookup, void *pUser)
{
    const StepDir *pStep;
    i32 nX;
    i32 nY;
    i32 nZ;
    bool bBlocked = false;

    if (pOrigin == NULL || pGeom == NULL || pfnLookup == NULL
        || nDir < 0 || nDir > 7) {
        return false;
    }
    pStep = &g_aStepDir[nDir];
    nX = (i32)pStep->dx + (i32)pOrigin->nX;
    nY = (i32)pStep->dy + (i32)pOrigin->nY;
    nZ = (i32)pOrigin->nZ;
    if (!Map_IsValidCell(pGeom, nX, nY, nZ)) {
        return false;
    }
    for (i32 v6 = 0; v6 < 4; ++v6) {
        MapCell *pCell;

        if (s_aExitProbe[nDir][v6][0] > 2) {
            break;
        }
        pCell = pfnLookup(nX + s_aExitProbe[nDir][v6][0],
                          nY + s_aExitProbe[nDir][v6][1], nZ, pUser);
        if (pCell == NULL) {
            continue;
        }
        if (s_aExitProbe[nDir][v6][2] == 2) {
            if (!MapCell_TestFlag(pCell, CELLFLAG_WALL_A_BLANK)
                && (!MapCell_TestFlag(pCell, CELLFLAG_WALL_A_BLOCKS)
                    || (pCell->bFlags29 & 4) != 0)) {
                if (pCell->pWallSprA != NULL && pCell->pWallSprA->gap_2f[0] != 0) {
                    bBlocked = true;
                }
            }
        } else {
            if (!MapCell_TestFlag(pCell, CELLFLAG_WALL_B_BLANK)
                && (!MapCell_TestFlag(pCell, CELLFLAG_WALL_B_BLOCKS)
                    || (pCell->bFlags29 & 4) != 0)) {
                if (pCell->pWallSprB != NULL && pCell->pWallSprB->gap_2f[0] != 0) {
                    bBlocked = true;
                }
            }
        }
    }
    return !bBlocked;
}

/* Unit_bArmorTuWarn @ 0x4e254. */
static i32 Unit_ArmorTuOf(const Soldier *pUnit, const ArmorHandInfo *pHand, i32 nSlot)
{
    i32 nCost;

    if (pHand == NULL || !pHand->bPresent) {
        return 0;
    }
    if (!Unit_bIsLarge(pUnit) && pHand->bTwoHanded != 0) {
        return 0;
    }
    nCost = (i32)pHand->abArmorTu[nSlot] * (i32)pUnit->auStat[0] / 100;
    if (nCost == 0 && nSlot == 0) {
        nCost = (i32)pHand->abArmorTu[1] * (i32)pUnit->auStat[0] / 100;
    }
    return nCost;
}

bool Unit_bArmorTuWarn(const Soldier *pUnit, const ArmorHandInfo *pHandL,
                       const ArmorHandInfo *pHandR, bool bOptAC81A, bool bOptAC83A,
                       bool bOptAC85A, i32 nTuCost,
                       void (*pfnPushStatus)(i32 nWhich, void *pUser), void *pUser)
{
    i32 nSlot;
    i32 nCostL;
    i32 nCostR;
    i32 nWarn;

    if (pUnit == NULL || bOptAC81A) {
        return false;
    }
    if (bOptAC83A) {
        nSlot = 1;
    } else if (bOptAC85A) {
        nSlot = 2;
    } else {
        nSlot = 0;
    }
    nCostL = Unit_ArmorTuOf(pUnit, pHandL, nSlot);
    nCostR = Unit_ArmorTuOf(pUnit, pHandR, nSlot);
    nWarn = (nCostL < nCostR) ? nCostR : nCostL;
    if (nWarn == 0) {
        return false;
    }
    /* Warn only when the unit can just afford the armour cost now but spending
     * nTuCost would drop it below that cost. */
    if ((i32)pUnit->uTu < nWarn || (i32)pUnit->uTu - nTuCost >= nWarn) {
        return false;
    }
    if (pfnPushStatus != NULL) {
        pfnPushStatus(0, pUser);
        pfnPushStatus(1, pUser);
    }
    return true;
}

/* Unit_SpotCellOccupant @ 0x48138. */
bool Unit_SpotCellOccupant(Soldier *pViewer, MapCell *pCell, SpotCtx *pCtx)
{
    Soldier *pOcc;
    bool bViewerOp;
    u8 bStatus;

    if (!MapCell_HasOccupant(pCell)) {
        return false;
    }
    pOcc = pCell->pUnit;
    bViewerOp = Unit_bIsOperative(pViewer);
    bStatus = pViewer->bStatus;
    if (!bViewerOp) {
        if (pOcc->bStatus == bStatus) {
            u8 nType = pOcc->nUnitType;
            if (nType != 16 && nType != 17) {
                return true;
            }
        }
    } else {
        u8 nOccStatus = pOcc->bStatus;
        u8 nType;
        if (nOccStatus == bStatus) {
            return true;
        }
        nType = pOcc->nUnitType;
        if (nType == 16 || nType == 17) {
            return true;
        }
    }
    /* Already in the viewer's multi-target list? */
    for (i32 i = 0; i < (i32)pViewer->nMultiTarget; ++i) {
        if (pViewer->multiOrAnim.apMultiTargets[i] == pOcc) {
            return true;
        }
    }
    pViewer->multiOrAnim.apMultiTargets[pViewer->nMultiTarget] = pOcc;
    pViewer->nMultiTarget += 1;

    if (!Unit_bIsOperative(pOcc) && pCtx->bBusy && bViewerOp) {
        if (pOcc->bUnk148 == 0 && pCtx->bQueueActive && pOcc == pCtx->pSelUnit) {
            if (pCtx->pfnFocusOnCell != NULL) {
                pCtx->pfnFocusOnCell(pCell, pCtx->pUser);
            }
            pCtx->pCameraUnit = pOcc;
        }
    }

    if (pCtx->bBusy) {
        for (i32 i = 0; i < pViewer->nSpottedCount; ++i) {
            if (pViewer->apSpotted[i] == pOcc) {
                return true;
            }
        }
        pViewer->apSpotted[pViewer->nSpottedCount] = pOcc;
        pViewer->nSpottedCount += 1;
    } else {
        if (pOcc->bUnk148 != 0) {
            return true;
        }
        pOcc->bUnk148 = 1;
    }
    if (pCtx->pnTrajFront1036 != NULL) {
        *pCtx->pnTrajFront1036 = 1;
    }
    return true;
}

/* Add one inventory item's weight to the burden. */
static void Unit_AddItemBurden(Soldier *pUnit, TactItem *pItem, const InvStatHooks *pHooks)
{
    u8 bWeight;
    u8 bTwoHanded;
    u8 bAmmoCap;

    if (pItem == NULL) {
        return;
    }
    bWeight = pHooks->pfnObjWeight(pItem, pHooks->pUser);
    bTwoHanded = pHooks->pfnObjTwoHanded(pItem, pHooks->pUser);
    bAmmoCap = pHooks->pfnObjAmmoCap(pItem, pHooks->pUser);
    pUnit->uInvBurden = (u16)(pUnit->uInvBurden + bWeight);
    /* A two-handed weapon counts as one item. */
    if (bTwoHanded != 0) {
        return;
    }
    /* A loaded weapon (ammo present, not unlimited) adds its ammo object too. */
    if (pItem->nAmmo == 0 || bAmmoCap == 255) {
        return;
    }
    pUnit->uInvBurden = (u16)(pUnit->uInvBurden
                             + pHooks->pfnAmmoWeight(pItem->nIndex, pHooks->pUser));
}

/* Unit_RecalcInventoryStat @ 0x49ef0. */
u16 Unit_RecalcInventoryStat(Soldier *pUnit, const InvStatHooks *pHooks)
{
    if (pUnit == NULL || pHooks == NULL) {
        return 0;
    }
    pUnit->uInvBurden = 1;
    Unit_AddItemBurden(pUnit, pUnit->pHandL, pHooks);
    Unit_AddItemBurden(pUnit, pUnit->pHandR, pHooks);
    for (i32 i = 0; i < 9; ++i) {
        Unit_AddItemBurden(pUnit, pUnit->apBelt[i], pHooks);
    }
    for (i32 i = 0; i < 8; ++i) {
        Unit_AddItemBurden(pUnit, pUnit->apBackpack[i], pHooks);
    }
    for (i32 i = 0; i < 4; ++i) {
        for (i32 j = 0; j < 2; ++j) {
            Unit_AddItemBurden(pUnit, pUnit->aPockets[i].apItem[j], pHooks);
        }
    }
    return pUnit->uInvBurden;
}

/* Cell-centre height used at both ends of a sight trace. */
static i32 Unit_LosZ(MapCell *pCell)
{
    i32 nZ = 12 * (i32)pCell->nZ;

    if (pCell->pCeilSpr != NULL) {
        nZ -= pCell->pCeilSpr->iCeilParam;
    }
    return nZ + 8;
}

i32 Unit_ScanVisibleUnits(Soldier *pViewer, const struct LosTables *pTables,
                          MapCellLookup pfnLookup, void *pUser,
                          SpotCtx *pSpot, const struct UnitRosters *pRosters,
                          Soldier **ppScratchUnit)
{
    i32 nTotal = pRosters->nXcom + pRosters->nEnemy;
    Soldier *pSelSave = pSpot->pSelUnit;
    MapCell *pViewCell = pViewer->pCell;

    if (pSpot->pnTrajFront1036 != NULL) {
        *pSpot->pnTrajFront1036 = 0;
    }
    pViewer->nMultiTarget = 0;
    pSpot->pSelUnit = pViewer;

    for (i32 i = 0; i < nTotal; ++i) {
        Soldier *pOther = (i < pRosters->nXcom)
                        ? pRosters->apXcom[i]
                        : pRosters->apEnemy[i - pRosters->nXcom];
        MapCell *pOtherCell;
        i32 nDx, nDy;

        if (pOther->bStatus == pViewer->bStatus
            && pOther->nUnitType != PUFO_UNIT_TYPE_SPOTTED_A
            && pOther->nUnitType != PUFO_UNIT_TYPE_SPOTTED_B) {
            continue;
        }

        pOtherCell = pOther->pCell;
        nDx = (i32)pViewCell->nX - (i32)pOtherCell->nX;
        nDy = (i32)pViewCell->nY - (i32)pOtherCell->nY;
        if ((i32)sqrt((double)nDx * nDx + (double)nDy * nDy) > PUFO_SCAN_RANGE_CELLS) {
            continue;
        }

        *ppScratchUnit = pOther;
        if (Map_TraceLos(pTables, pfnLookup, pUser,
                         16 * (i32)pViewCell->nX + 8, 16 * (i32)pViewCell->nY + 8,
                         Unit_LosZ(pViewCell),
                         16 * (i32)pOtherCell->nX + 8, 16 * (i32)pOtherCell->nY + 8,
                         Unit_LosZ(pOtherCell), NULL, NULL)) {
            Unit_SpotCellOccupant(pViewer, pOtherCell, pSpot);
        }
        *ppScratchUnit = NULL;
    }

    pSpot->pSelUnit = pSelSave;
    *ppScratchUnit = NULL;
    return 0;
}

/* --- Unit_UpdateVisibility @ 0x48580 -------------------------------------- */

/* The loft row a sprite uses at a given layer. The original indexes with
 * `(12 * nLayer + 8) % 12 + 8`, which always lands on 16 because 12 * nLayer is a
 * whole number of layers: the row id is the sprite byte at
 * PUFO_MC1_ROWTAB_TO_Z + 8. The expression is kept as the original writes it so
 * the shape stays visible. */
static u32 Unit_LoftRowId(const Mc1Rec *pSpr, i32 nLayer)
{
    const u8 *pBytes = (const u8 *)pSpr;

    return (u32)pBytes[PUFO_MC1_ROWTAB_TO_Z + ((12 * nLayer + 8) % PUFO_VOX_PER_CELL_Z)];
}

static bool Unit_bLoftBlocks(const LosTables *pTables, const Mc1Rec *pSpr, i32 nLayer,
                             i32 nVoxX, i32 nVoxY)
{
    u32 nRow = (u32)(nVoxY % PUFO_VOX_PER_CELL_Y)
             + PUFO_VOX_PER_CELL_Y * Unit_LoftRowId(pSpr, nLayer);

    return Los_TestRowBit(pTables->pLoftemps, pTables->nLoftemps, nRow,
                          (u32)(nVoxX % PUFO_VOX_PER_CELL_X));
}

/* A wall stops the ray when its 0x2b byte is set and the voxel is covered, and
 * then only while the cell has not been opened: sprite byte 0x2a zero, or the
 * cell's bFlags29 bit 2 clear. bBoundaryCase carries the original's extra rule for
 * wall A, that a voxel landing exactly on a cell boundary counts as covered. */
static bool Unit_bRayBlockedByWall(const LosTables *pTables, const MapCell *pCell,
                                   const Mc1Rec *pSpr, i32 nLayer,
                                   i32 nVoxX, i32 nVoxY, bool bBoundaryCase)
{
    if (pSpr == NULL || pSpr->bUnk2B == 0) {
        return false;
    }
    if (!(bBoundaryCase && (nVoxX % PUFO_VOX_PER_CELL_X) == 0)
        && !Unit_bLoftBlocks(pTables, pSpr, nLayer, nVoxX, nVoxY)) {
        return false;
    }
    return pSpr->bBlocksMove == 0 || (pCell->bFlags29 & 4u) == 0;
}

/* The ceiling id is the u16 at sprite +0x28, which is Mc1Rec.nGlobalIdx. The
 * original reaches it as *(u16 *)&ObjectRec->bTwoHanded, the same two bytes seen
 * through the item record. */
static bool Unit_bCeilingAllowed(u8 nMissionType, const Mc1Rec *pCeil)
{
    u16 nId = pCeil->nGlobalIdx;

    if (nMissionType == PUFO_MISSION_TYPE_DESERT) {
        return nId >= PUFO_CEIL_ID_DESERT_MIN && nId <= PUFO_CEIL_ID_DESERT_MAX;
    }
    if (nMissionType == PUFO_MISSION_TYPE_FOREST) {
        return (nId >= PUFO_CEIL_ID_FOREST_MIN && nId <= PUFO_CEIL_ID_FOREST_MAX)
            || nId == PUFO_CEIL_ID_FOREST_ALT;
    }
    return false;
}

/* Reveal the column through a cell the ray has entered, upward and downward.
 * Up runs from nLayer while the layer stays under 4 and stops when the cell above
 * has a floor sprite. Down runs from nLayer to layer 0 and stops at the first cell
 * that has one. Both directions include nLayer itself, and the original then spots
 * the ray's own cell a third time. */
static void Unit_RevealColumn(VisCtx *pCtx, Soldier *pUnit,
                              i32 nCellX, i32 nCellY, i32 nLayer)
{
    MapCell *pRayCell = NULL;

    if (nLayer < 4) {
        for (i32 nL = nLayer; nL < 4; ++nL) {
            MapCell *pC = pCtx->pfnLookup(nCellX, nCellY, nL, pCtx->pUser);

            if (pC == NULL) {
                break;
            }
            if (nL == nLayer) {
                pRayCell = pC;
            }
            if (Unit_bIsOperative(pUnit)) {
                MapCell_SetBFlags29_4(pC);
                pC->bFlags44 |= 1u;
            }
            Unit_SpotCellOccupant(pUnit, pC, pCtx->pSpot);
            if (nL < 3) {
                MapCell *pUp = pCtx->pfnLookup(nCellX, nCellY, nL + 1, pCtx->pUser);

                if (pUp != NULL && pUp->pFloorSpr != NULL) {
                    break;
                }
            }
        }
    }

    for (i32 nL = nLayer; nL >= 0; --nL) {
        MapCell *pC = pCtx->pfnLookup(nCellX, nCellY, nL, pCtx->pUser);

        if (pC == NULL) {
            break;
        }
        if (nL == nLayer) {
            pRayCell = pC;
        }
        if (Unit_bIsOperative(pUnit)) {
            MapCell_SetBFlags29_4(pC);
            pC->bFlags44 |= 1u;
        }
        Unit_SpotCellOccupant(pUnit, pC, pCtx->pSpot);
        if (pC->pFloorSpr != NULL) {
            break;
        }
    }
    if (pRayCell != NULL) {
        Unit_SpotCellOccupant(pUnit, pRayCell, pCtx->pSpot);
    }
}

i32 Unit_UpdateVisibility(Soldier *pUnit, VisCtx *pCtx)
{
    MapCell *pOwn = pUnit->pCell;
    const i32 nOwnX = pOwn->nX;
    const i32 nOwnY = pOwn->nY;
    const i32 nOwnZ = pOwn->nZ;
    MapCell *pOwnCell;
    Mc1Rec *pOwnCeil;
    i32 nLayer;

    if (!Unit_bIsOperative(pUnit)) {
        pUnit->nMultiTarget = 0;
        return Unit_ScanVisibleUnits(pUnit, pCtx->pTables, pCtx->pfnLookup, pCtx->pUser,
                                     pCtx->pSpot, pCtx->pRosters, pCtx->ppScratchUnit);
    }

    if (pCtx->pSpot->pnTrajFront1036 != NULL) {
        *pCtx->pSpot->pnTrajFront1036 = 0;
    }
    pUnit->nMultiTarget = 0;

    MapCell_SetBFlags29_4(pOwn);
    if (Unit_bIsLarge(pUnit)) {
        static const i32 aDX[3] = { 1, 0, 1 };
        static const i32 aDY[3] = { 0, 1, 1 };

        for (i32 k = 0; k < 3; ++k) {
            MapCell *pC = pCtx->pfnLookup(nOwnX + aDX[k], nOwnY + aDY[k], nOwnZ,
                                          pCtx->pUser);

            if (pC == NULL) {
                continue;
            }
            MapCell_SetBFlags29_4(pC);
            pC->bFlags44 |= 1u;
        }
    }

    /* Down the column: a cell is revealed when the cell one layer above it has no
     * occupant, or its occupant's byte 0x2E is 1. The walk reaches layer 0. */
    for (i32 nL = nOwnZ - 1; nL >= 0; --nL) {
        MapCell *pC = pCtx->pfnLookup(nOwnX, nOwnY, nL, pCtx->pUser);
        MapCell *pAbove = pCtx->pfnLookup(nOwnX, nOwnY, nL + 1, pCtx->pUser);

        if (pC == NULL || pAbove == NULL) {
            break;
        }
        if (pAbove->pUnit == NULL || pAbove->pUnit->multiOrAnim.anim.bUnk12 == 1) {
            MapCell_SetBFlags29_4(pC);
            pC->bFlags44 |= 1u;
            Unit_SpotCellOccupant(pUnit, pC, pCtx->pSpot);
        }
    }

    /* Up the column: a cell is revealed when its own floor sprite is absent or its
     * bBlocksSight byte is 1. */
    for (i32 nL = nOwnZ + 1; nL < 4; ++nL) {
        MapCell *pC = pCtx->pfnLookup(nOwnX, nOwnY, nL, pCtx->pUser);

        if (pC == NULL) {
            break;
        }
        if (pC->pFloorSpr == NULL || pC->pFloorSpr->bBlocksSight == 1) {
            MapCell_SetBFlags29_4(pC);
            pC->bFlags44 |= 1u;
            Unit_SpotCellOccupant(pUnit, pC, pCtx->pSpot);
        }
    }

    /* A low ceiling lifts the working layer by one, so the fan is walked at the
     * height the unit is actually seen at. */
    nLayer = nOwnZ;
    pOwnCell = pCtx->pfnLookup(nOwnX, nOwnY, nOwnZ, pCtx->pUser);
    pOwnCeil = pOwnCell->pCeilSpr;
    if (pOwnCeil != NULL && pOwnCeil->iCeilParam < -10) {
        nLayer = nOwnZ + 1;
    }

    for (i32 iRay = 0; iRay < PUFO_VIS_RAY_COUNT; ++iRay) {
        const VisRay *pRay = &pCtx->apRays[iRay];
        double dRun;
        double dStepX;
        double dStepY;
        double dBudget = PUFO_VIS_BUDGET;
        MapCell *pPrev = NULL;

        if (pRay->aPass[pUnit->bFacing] == 0) {
            continue;
        }
        dRun = sqrt((double)pRay->nDx * pRay->nDx
                  + (double)pRay->nDy * pRay->nDy) * 16.0;
        dStepX = ((double)(16 * pRay->nDx) + 8.0) / dRun;
        dStepY = ((double)(16 * pRay->nDy) + 8.0) / dRun;

        for (i32 t = 0; t < PUFO_VIS_MAX_STEPS; ++t) {
            i32 nVoxX = (i32)((double)(16 * nOwnX + 8) + (double)t * dStepX);
            i32 nVoxY = (i32)((double)(16 * nOwnY + 8) + (double)t * dStepY);
            i32 nCellX = nVoxX / PUFO_VOX_PER_CELL_X;
            i32 nCellY = nVoxY / PUFO_VOX_PER_CELL_Y;
            MapCell *pCell;
            u32 nCost;

            if (!Map_IsValidCell(pCtx->pGeom, nCellX, nCellY, nLayer)) {
                break;
            }
            pCell = pCtx->pfnLookup(nCellX, nCellY, nLayer, pCtx->pUser);
            if (pCell == NULL) {
                break;
            }

            nCost = pCell->nFlags32 & 7u;
            if (nCost != 0u && nCost != 4u) {
                double dSub = (double)(2u * nCost);

                dBudget = (dBudget > dSub) ? (dBudget - dSub) : 0.0;
            }
            if (!((double)t <= dBudget)) {
                break;
            }

            if (Unit_bRayBlockedByWall(pCtx->pTables, pCell, pCell->pWallSprA, nLayer,
                                       nVoxX, nVoxY, true)) {
                break;
            }
            if (Unit_bRayBlockedByWall(pCtx->pTables, pCell, pCell->pWallSprB, nLayer,
                                       nVoxX, nVoxY, false)) {
                break;
            }

            if (pCell != pPrev) {
                Unit_RevealColumn(pCtx, pUnit, nCellX, nCellY, nLayer);
                pPrev = pCell;
            }

            if (pCell->pCeilSpr != NULL
                && Unit_bLoftBlocks(pCtx->pTables, pCell->pCeilSpr, nLayer,
                                    nVoxX, nVoxY)
                && !Unit_bCeilingAllowed(pCtx->nMissionType, pCell->pCeilSpr)) {
                break;
            }
        }
    }

    /* Multi-target scan over the enemy roster. */
    for (i32 i = 0; i < pCtx->pRosters->nEnemy; ++i) {
        Soldier *pEnemy = pCtx->pRosters->apEnemy[i];
        MapCell *pEnemyCell = pEnemy->pCell;
        i32 nDx = nOwnX - pEnemyCell->nX;
        i32 nDy = nOwnY - pEnemyCell->nY;
        i32 nDist = (i32)sqrt((double)nDx * nDx + (double)nDy * nDy);
        double dBearing;
        double dFace;
        bool bInView;

        if (nDist > PUFO_VIS_SIGHT_RANGE || nDist < 1) {
            continue;
        }

        dBearing = Unit_CellBearing(pOwn, pEnemyCell);
        dFace = Unit_FacingAngle(pUnit);
        if (pUnit->bFacing == 2) {
            bInView = dBearing >= PUFO_FACING_STEP && dBearing <= PUFO_VIS_ANGLE_MAX;
        } else {
            i32 nErr = (i32)(dBearing - dFace);

            bInView = (double)(nErr < 0 ? -nErr : nErr) <= PUFO_FACING_STEP;
        }
        if (!bInView) {
            continue;
        }

        *pCtx->ppScratchUnit = pEnemy;
        {
            Soldier *pSelSave = pCtx->pSpot->pSelUnit;

            pCtx->pSpot->pSelUnit = pUnit;
            if (Map_TraceLos(pCtx->pTables, pCtx->pfnLookup, pCtx->pUser,
                             16 * nOwnX + 8, 16 * nOwnY + 8, Unit_LosZ(pOwn),
                             16 * pEnemyCell->nX + 8, 16 * pEnemyCell->nY + 8,
                             Unit_LosZ(pEnemyCell), NULL, NULL)) {
                Unit_SpotCellOccupant(pUnit, pEnemyCell, pCtx->pSpot);
                pEnemyCell->bFlags29 |= 0x10u;
                pEnemyCell->bFlags44 |= 1u;
            }
            pCtx->pSpot->pSelUnit = pSelSave;
        }
        *pCtx->ppScratchUnit = NULL;
    }

    return 0;
}

/* Unit_bTryOpportunityFire @ 0x47c38.
 *
 * Note the argument order: the target comes first.
 *
 * The gate chain is distance, facing cone, the angle duel, then the fire-mode
 * pick. All four use helpers the port already has, and the facing cone is the
 * same rule the multi-target scan inside Unit_UpdateVisibility uses, with the
 * same three constants and the same truncation of the angle error.
 *
 * The arc is traced FROM THE TARGET TO THE SHOOTER, and both ends use the plain
 * `12 * nZ + 8`. That is unlike the Map_TraceLos calls in Unit_UpdateVisibility
 * and Unit_ScanVisibleUnits, which subtract the ceiling sprite's nObjParam. The
 * same pair of cell heights is spelled two ways in the same subsystem, and this
 * one has no ceiling term.
 *
 * After a clear arc the shooter's visibility is refreshed and the target is
 * looked for in the shooter's multi-target list. Only a real match enqueues the
 * shooter into the opportunity-fire queue. */
bool Unit_bTryOpportunityFire(Soldier *pTarget, Soldier *pShooter,
                              const struct FireGate *pGate, TactItem **ppSelWeapon,
                              i32 *pnActionMode, struct ActionCostTable *pCosts,
                              VisCtx *pCtx)
{
    MapCell *pTargetCell = pTarget->pCell;
    MapCell *pShooterCell = pShooter->pCell;
    const i32 nDx = pTargetCell->nX - pShooterCell->nX;
    const i32 nDy = pTargetCell->nY - pShooterCell->nY;
    const i32 nDist = (i32)sqrt((double)nDx * nDx + (double)nDy * nDy);
    double dBearing;
    double dFace;
    bool bInView;

    if (nDist > PUFO_VIS_SIGHT_RANGE || nDist < 1) {
        return false;
    }

    dBearing = Unit_CellBearing(pShooterCell, pTargetCell);
    dFace = Unit_FacingAngle(pShooter);
    if (pShooter->bFacing == 2) {
        bInView = dBearing >= PUFO_FACING_STEP && dBearing <= PUFO_VIS_ANGLE_MAX;
    } else {
        i32 nErr = (i32)(dBearing - dFace);

        bInView = (double)(nErr < 0 ? -nErr : nErr) <= PUFO_FACING_STEP;
    }
    if (!bInView) {
        return false;
    }

    if (!Unit_bFacingAngleDuel(pShooter, pTarget)) {
        return false;
    }
    if (!Unit_SelectBestFireMode(pShooter, pGate, ppSelWeapon, pnActionMode, pCosts)) {
        return false;
    }

    *pCtx->ppScratchUnit = pShooter;

    if (!Tact_TraceProjectileArc(pCtx->pAim, pCtx->pTables, pCtx->pGeom,
                                 pCtx->pfnLookup, pCtx->pUser, NULL, NULL,
                                 16 * pTargetCell->nX + 8, 16 * pTargetCell->nY + 8,
                                 12 * pTargetCell->nZ + 8,
                                 16 * pShooterCell->nX + 8, 16 * pShooterCell->nY + 8,
                                 12 * pShooterCell->nZ + 8, 0, NULL)) {
        *pCtx->ppScratchUnit = NULL;
        if (pCtx->pbFireStatus1 != NULL) {
            *pCtx->pbFireStatus1 = 0;
        }
        return false;
    }

    Unit_UpdateVisibility(pShooter, pCtx);

    if (pShooter->nMultiTarget > 0) {
        const i32 nCount = pShooter->nMultiTarget;

        for (i32 i = 0; i < nCount; ++i) {
            if (pShooter->multiOrAnim.apMultiTargets[i] == pTarget) {
                if (pCtx->pbFireStatus1 != NULL) {
                    *pCtx->pbFireStatus1 = 0;
                }
                *pCtx->ppScratchUnit = NULL;
                pCtx->pTactCtx->apOppFire[pCtx->pTactCtx->nOppFireCount] = pShooter;
                pCtx->pTactCtx->nOppFireCount += 1;
                return true;
            }
        }

        /* Kept as the original has it. When the target is not in the list the
         * search loop exits with the shooter pointer still in its result register,
         * so the function reports success, leaves anActionPhaseSave0[120] pointing
         * at the shooter, does not clear bFireStatus0[1], and enqueues nothing. */
        return true;
    }

    return false;
}

/* Unit_DropAllItemsToCell @ 0x4b4f8.
 *
 * Drops everything the unit carries into the cell it stands on, clearing each slot
 * as it goes. Large units keep their hands: the two hand slots are the only ones
 * behind a Unit_bIsLarge guard.
 *
 * unk_138EA0 is the shared empty-slot placeholder. A slot may hold it instead of
 * NULL. It counts as "no item" for dropping, but the slot is still cleared, so the
 * placeholder does not survive a drop.
 *
 * Slot map. The decompiler renders the belt walk through a MapCell cast, so the
 * offsets are worth spelling out: it reads v7[6].pWallSprA with v7 advancing four
 * bytes at a time, which is 4*n + 6*sizeof(MapCell) + offsetof(pWallSprA) =
 * 0x16c + 4*n, i.e. apBelt[n]. The backpack walk is i in 0..1, j in 0..3 with
 * "i != 1 || (j != 1 && j != 2)", so slots 5 and 6 are skipped. The pocket walk
 * runs twice with v12 advancing four bytes, which lands on apItem[0] of the four
 * pockets and then on apItem[1] of the same four.
 *
 * The original returns a MapCell pointer that is just the last register value
 * threaded through the inserts; no caller uses it. */
TactItem g_itemSlotEmpty;

static void Unit_DropSlot(MapCell *pCell, TactItem **ppItem)
{
    TactItem *pItem = *ppItem;

    if (pItem == NULL) {
        return;
    }
    if (pItem != &g_itemSlotEmpty) {
        MapCell_ListInsertByPri(pCell, pItem);
    }
    *ppItem = NULL;
}

void Unit_DropAllItemsToCell(Soldier *pUnit)
{
    if (!Unit_bIsLarge(pUnit)) {
        Unit_DropSlot(pUnit->pCell, &pUnit->pHandL);
        Unit_DropSlot(pUnit->pCell, &pUnit->pHandR);
    }

    for (i32 n = 0; n < 9; ++n) {
        Unit_DropSlot(pUnit->pCell, &pUnit->apBelt[n]);
    }

    for (i32 n = 0; n < 8; ++n) {
        if (n == 5 || n == 6) {
            continue;
        }
        Unit_DropSlot(pUnit->pCell, &pUnit->apBackpack[n]);
    }

    for (i32 nPocket = 0; nPocket < 4; ++nPocket) {
        for (i32 nItem = 0; nItem < 2; ++nItem) {
            Unit_DropSlot(pUnit->pCell, &pUnit->aPockets[nPocket].apItem[nItem]);
        }
    }
}

/* UnitList_ForEachRefresh @ 0x22b54. Refresh the visibility of every Xcom unit.
 * The original threads the last Unit_UpdateVisibility result through as its own
 * return value; no caller uses it.
 *
 * The original walks pMap->apXcomUnits / nXcomUnitCount. The port passes the same
 * two through UnitRosters, which is already the roster pair every other unit-list
 * walk uses. */
void UnitList_ForEachRefresh(UnitRosters *pRosters, VisCtx *pCtx)
{
    for (i32 n = 0; n < pRosters->nXcom; ++n) {
        Unit_UpdateVisibility(pRosters->apXcom[n], pCtx);
    }
}

/* SideLists is the same two rosters UnitRosters already carries, seen through the
 * type Tact_RemoveUnitFromSideLists works on. Built locally so there is one source
 * of truth for the rosters. */
static SideLists Kill_SideLists(const UnitRosters *pR)
{
    SideLists lists;

    lists.apXcom  = pR->apXcom;
    lists.nXcom   = pR->nXcom;
    lists.apEnemy = pR->apEnemy;
    lists.nEnemy  = pR->nEnemy;
    return lists;
}

/* Copy a NUL-terminated string into a fixed field, truncating rather than
 * overflowing. The original's copy loop is byte-at-a-time with the same stop. */
static void Kill_CopyText(char *pDst, size_t cbDst, const char *pSrc)
{
    size_t n = 0;

    while (n + 1 < cbDst && pSrc[n] != '\0') {
        pDst[n] = pSrc[n];
        ++n;
    }
    pDst[n] = '\0';
}

static void Kill_AppendText(char *pDst, size_t cbDst, const char *pSrc)
{
    size_t n = 0;

    while (n < cbDst && pDst[n] != '\0') {
        ++n;
    }
    while (n + 1 < cbDst && pSrc != NULL && pSrc[0] != '\0') {
        pDst[n++] = *pSrc++;
    }
    if (n < cbDst) {
        pDst[n] = '\0';
    }
}

/* --- Tact_KillUnit @ 0x4e430 -------------------------------------------------
 *
 * The three tables this reads are named in the IDB and are read nowhere else in
 * the binary. g_aDeathSoundByType is 23 entries, one per unit type 0..22; index 23
 * onward is unrelated data. g_aCorpseObjByType is indexed by unit type.
 * g_aCorpseObjQuadrant is indexed 4 * type + q with q = 1, 2, 3 for the three extra
 * cells of a large unit's 2x2 footprint.
 */
static const u16 g_aDeathSoundByType[23] = {
     0,  0,  0,  0, 18, 18, 18, 18, 18, 24, 25,
    25, 24, 23, 28, 23, 22,  0, 18, 23, 18, 18, 29
};

static const u16 g_aCorpseObjByType[22] = {
    70, 71, 72, 72, 75, 75, 75, 76, 76, 59, 62, 61,
    60, 63, 65, 67, 74, 73, 99, 68, 66, 69
};

/* Types 0..3 and 9..11 have no quadrant corpses. */
static const u16 g_aCorpseObjQuadrant[48] = {
    0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0,
    75, 125, 126, 127,  75, 125, 126, 127,  75, 125, 126, 127,
    76, 128, 129, 130,  76, 128, 129, 130,
    0, 0, 0, 0,   0, 0, 0, 0,   0, 0, 0, 0
};

/* The chryssalid transform loads its stats from the 32-byte-per-type template at
 * unk_A8F38 + 32 * 14 = 0xA8FD8. Dumped from the IDB. */
static const u16 g_aChryssalidTemplate[16] = {
    80, 60, 100, 40, 110, 0, 0, 0, 15, 5, 5, 20, 5, 5, 0, 0
};

static u16 Kill_QuadrantObj(u8 nUnitType, i32 nQuadrant)
{
    i32 nIndex = 4 * (i32)nUnitType + nQuadrant;

    if (nIndex < 0 || nIndex >= (i32)(sizeof(g_aCorpseObjQuadrant) / sizeof(u16))) {
        return 0;
    }
    return g_aCorpseObjQuadrant[nIndex];
}

/* Place one corpse item on the lowest cell at or below nLayer that has a floor
 * sprite. The walk keeps the cell's own nX / nY and steps the layer index down,
 * which is what the ARM does: the index is built from pCell->nY and pCell->nX and
 * the layer term is decremented by 60 before it is multiplied by 60 again.
 *
 * Returns the layer the corpse landed on, or -1 when no cell had a floor sprite. */
static i32 Kill_DropCorpse(const KillCtx *pCtx, MapCell *pCell, Soldier *pUnit,
                           i32 nQuadrant)
{
    i32 nLayer = pCell->nZ;

    while (nLayer >= 0) {
        MapCell *pProbe = pCtx->pCombat->pfnLookup(pCell->nX, pCell->nY, nLayer,
                                                   pCtx->pCombat->pUser);
        if (pProbe != NULL && pProbe->pFloorSpr != NULL) {
            break;
        }
        --nLayer;
    }
    if (nLayer < 0) {
        return -1;
    }

    MapCell    *pLand = pCtx->pCombat->pfnLookup(pCell->nX, pCell->nY, nLayer,
                                                 pCtx->pCombat->pUser);
    TactItem   *pItem   = pCtx->apTactItems[*pCtx->pnTactItem];
    u16         nObjIdx = (nQuadrant == 0)
                        ? g_aCorpseObjByType[pUnit->nUnitType]
                        : Kill_QuadrantObj(pUnit->nUnitType, nQuadrant);

    /* The original moves the unit onto the cell its corpse landed on (pUnit->pCell
     * = v36 at 0x4eaa8) before it places the item, and only for the first corpse.
     * Everything after the kill - fall damage, the drop, the sweep - reads the unit
     * from that cell. */
    if (nQuadrant == 0) {
        pUnit->pCell = pLand;
    }

    TactItem_ctor(pItem);
    pItem->pObj = pCtx->apObjects[nObjIdx];
    /* The original stores the unit pointer in the item's linked-soldier field only
     * for a unit that died with hit points; one already at zero HP leaves the field
     * as the constructor left it. */
    pItem->pLinkedSoldier = (pUnit->uHp != 0) ? pUnit : NULL;

    MapCell_ListInsertByPri(pLand, pItem);
    ++*pCtx->pnTactItem;
    return nLayer;
}

void Tact_KillUnit(const KillCtx *pCtx, const KillHooks *pHooks, Soldier *pUnit)
{
    if ((pUnit->bFlags & 0x20) != 0) {
        return; /* already dead */
    }

    pUnit->nSpottedCount = 0;
    pUnit->nMultiTarget  = 0;

    /* A non-human type that reaches zero HP feeds the mission kill counter at
     * Map.gap_TrajRest + 4 * 49940. Types 16 and 17 are excluded. */
    if (pUnit->nUnitType > 8 && pUnit->nUnitType != 16 && pUnit->nUnitType != 17
        && pUnit->uHp == 0 && pCtx->pnKillCount != NULL) {
        ++*pCtx->pnKillCount;
    }

    /* Death sound. Types 0..2 with bFemale set play the fixed sample 22; everything
     * else indexes g_aDeathSoundByType, and a type >= 3 that already has bFlags bit
     * 5 set plays nothing at all. */
    if (pUnit->nUnitType >= 3) {
        if ((pUnit->bFlags & 0x20) == 0 && pUnit->nUnitType < 23) {
            Sound_Play((i32)g_aDeathSoundByType[pUnit->nUnitType]);
        }
    } else if (pUnit->bFemale != 0) {
        Sound_Play(22);
    } else if (pUnit->nUnitType < 23) {
        Sound_Play((i32)g_aDeathSoundByType[pUnit->nUnitType]);
    }

    if (pUnit->nUnitType == 22) {
        /* The chryssalid transform. This path does not kill the unit: it rewrites
         * it as unit type 14 and returns. */
        SideLists lists = Kill_SideLists(pCtx->pRosters);

        SideLists_RemoveUnit(&lists, pUnit);
        pUnit->bStatus    = 1;
        pUnit->nUnitType  = 14;

        if (pCtx->pszTransformName != NULL) {
            Kill_CopyText(pUnit->szName, sizeof(pUnit->szName), pCtx->pszTransformName);
        }

        const u16 *pTpl = g_aChryssalidTemplate;
        pUnit->auStat[0] = pTpl[0];
        pUnit->uTu       = pTpl[0];
        pUnit->auStat[9] = pTpl[1];
        pUnit->auStat[1] = (u16)(Game_RandU16(0x14) - 10 + pTpl[2]);
        pUnit->auStat[2] = (u16)(Game_RandU16(0x14) - 10 + pTpl[3]);
        pUnit->auStat[3] = (u16)(Game_RandU16(0x14) - 10 + pTpl[4]);
        /* The original writes auStat[4] twice, from template words 14 and then 15,
         * so the second write is the one that survives. */
        pUnit->auStat[4] = pTpl[14];
        pUnit->auStat[4] = pTpl[15];
        pUnit->auStat[8] = (u16)(Game_RandU16(0x14) - 10 + pTpl[1]);
        pUnit->bUnk248   = (u8)pTpl[8];
        for (i32 n = 0; n < 5; ++n) {
            pUnit->abReady252[n] = (u8)pTpl[9 + n];
        }

        pUnit->bUnk422 = 0;
        Unit_PlaceOnMap(pUnit, pUnit->pCell->nX, pUnit->pCell->nY, pUnit->pCell->nZ,
                        pCtx->pCombat->pfnLookup, pCtx->pCombat->pUser);
        return;
    }

    pUnit->bFlags |= 0x20u;
    /* bFlags bit 5 is set, so this takes the early return that pins the pose to 18
     * and masks the other animation bits off. */
    Unit_UpdateAnimPose(pUnit, NULL, NULL);
    {
        SideLists lists = Kill_SideLists(pCtx->pRosters);
        SideLists_RemoveUnit(&lists, pUnit);
    }

    /* Morale shock, and its sign depends on whether the dead unit was human-sized.
     * The +34 branch has no low clamp: a negative result wraps through LOBYTE, so
     * the byte assignment below is part of the behaviour, not an accident. */
    {
        Soldier **apEnemy = pCtx->pRosters->apEnemy;
        i32       nEnemy  = pCtx->pRosters->nEnemy;
        Soldier **apXcom  = pCtx->pRosters->apXcom;
        i32       nXcom   = pCtx->pRosters->nXcom;

        if (pUnit->nUnitType > 8) {
            if (pUnit->nUnitType != 16 && pUnit->nUnitType != 17) {
                for (i32 n = 0; n < nEnemy; ++n) {
                    i32 v = apEnemy[n]->bMorale
                          - 500 / (3 * (i32)apEnemy[n]->auStat[2]);
                    apEnemy[n]->bMorale = (v < 0) ? 0 : (u8)v;
                }
                for (i32 n = 0; n < nXcom; ++n) {
                    i32 v = apXcom[n]->bMorale
                          - 1000 / (3 * (i32)apXcom[n]->auStat[2]) + 34;
                    apXcom[n]->bMorale = (v > 100) ? 100 : (u8)v;
                }
            }
        } else {
            for (i32 n = 0; n < nEnemy; ++n) {
                i32 v = apEnemy[n]->bMorale
                      - 1000 / (3 * (i32)apEnemy[n]->auStat[2]) + 34;
                apEnemy[n]->bMorale = (v > 100) ? 100 : (u8)v;
            }
            for (i32 n = 0; n < nXcom; ++n) {
                i32 v = apXcom[n]->bMorale
                      - 1000 / (3 * (i32)apXcom[n]->auStat[2]);
                apXcom[n]->bMorale = (v < 0) ? 0 : (u8)v;
            }
        }
    }

    MapCell *pCell = pUnit->pCell;
    pUnit->bStatus = 4;
    MapCell_SetOccupant(pCell, NULL);
    if (Unit_bIsLarge(pUnit)) {
        MapCell_SetOccupant(pCtx->pCombat->pfnLookup(pCell->nX + 1, pCell->nY,
                                                     pCell->nZ, pCtx->pCombat->pUser), NULL);
        MapCell_SetOccupant(pCtx->pCombat->pfnLookup(pCell->nX, pCell->nY + 1,
                                                     pCell->nZ, pCtx->pCombat->pUser), NULL);
        MapCell_SetOccupant(pCtx->pCombat->pfnLookup(pCell->nX + 1, pCell->nY + 1,
                                                     pCell->nZ, pCtx->pCombat->pUser), NULL);
    }

    if (*pCtx->ppSelUnit == pUnit) {
        *pCtx->ppSelUnit = pCtx->pRosters->apXcom[0];
    }

    /* Corpses live on the civilian roster. */
    pCtx->pRosters->apCiv[pCtx->pRosters->nCiv++] = pUnit;

    i32 nLandLayer = Kill_DropCorpse(pCtx, pCell, pUnit, 0);
    if (Unit_bIsLarge(pUnit)) {
        MapCell_SetOccupant(pCtx->pCombat->pfnLookup(pCell->nX + 1, pCell->nY,
                                                     pCell->nZ, pCtx->pCombat->pUser), NULL);
        Kill_DropCorpse(pCtx, pCell, pUnit, 1);
        MapCell_SetOccupant(pCtx->pCombat->pfnLookup(pCell->nX, pCell->nY + 1,
                                                     pCell->nZ, pCtx->pCombat->pUser), NULL);
        Kill_DropCorpse(pCtx, pCell, pUnit, 2);
        MapCell_SetOccupant(pCtx->pCombat->pfnLookup(pCell->nX + 1, pCell->nY + 1,
                                                     pCell->nZ, pCtx->pCombat->pUser), NULL);
        Kill_DropCorpse(pCtx, pCell, pUnit, 3);
    }

    /* Fall damage. Confirmed from the ARM at 0x4eefc: the distance is the landed
     * cell's own stored nZ byte minus the layer index the walk stopped at, minus
     * one. Those are two different quantities, and when a map stores them equal the
     * term is -1, the product clamps to zero, and the kill does no fall damage. */
    {
        MapCell *pLand = pCtx->pCombat->pfnLookup(pCell->nX, pCell->nY, nLandLayer,
                                                  pCtx->pCombat->pUser);
        i32 nFall = (nLandLayer < 0) ? -1 : (i32)pLand->nZ - nLandLayer - 1;
        i32 nDmg  = (i32)((float)nFall * (float)pUnit->auStat[1] * 0.74f);

        if (nDmg < 0) {
            nDmg = 0;
        }
        if ((i32)pUnit->uHp > nDmg) {
            pUnit->uHp    = (u16)((i32)pUnit->uHp - nDmg);
            pUnit->nUnk436 += nDmg;
        } else {
            pUnit->uHp = (u16)pUnit->nUnk436;
        }
    }

    /* A chryssalid transform keeps its gear; a large unit drops items per corpse
     * instead of through this path. */
    if (!Unit_bIsLarge(pUnit) && pUnit->nUnitType != 14) {
        Unit_DropAllItemsToCell(pUnit);
    }

    if (pHooks->pfnCheckBattleEnd != NULL) {
        pHooks->pfnCheckBattleEnd(pHooks->pUser);
    }

    if (pCtx->bTickHold || pCtx->bFrameScratch) {
        return;
    }

    if (pUnit->bUnk1037 == 0) {
        if (pUnit->nUnitType > 8) {
            pUnit->bUnk1037 = 1; /* no message for these */
        } else {
            const char *pszTail;

            if (pUnit->uHp != 0) {
                pszTail = (pUnit->bFemale != 0) ? pCtx->pszMsgAliveFemale
                                                : pCtx->pszMsgAliveMale;
            } else if (pUnit->nUnitType < 4) {
                pszTail = (pUnit->bFemale != 0) ? pCtx->pszMsgDeadFemale
                                                : pCtx->pszMsgDeadMaleSmall;
            } else {
                pszTail = pCtx->pszMsgDeadMaleBig;
            }
            if (pszTail != NULL) {
                char szMsg[200];

                Kill_CopyText(szMsg, sizeof(szMsg), pUnit->szName);
                Kill_AppendText(szMsg, sizeof(szMsg), pszTail);
                StatusMsg_Push(pCtx->pStatus, szMsg, true, pCtx->pStatusHooks);
            }
        }
    }

    /* Type 18 goes up in an explosion that damages its own cell and the three
     * large-unit neighbours. */
    if (pUnit->nUnitType == 18 && Game_RandU16(0xA) != 0) {
        pCtx->pCombat->nShotOriginX = pUnit->pCell->nX;
        pCtx->pCombat->nShotOriginY = pUnit->pCell->nY;
        Combat_ResolveShot(pCtx->pCombat, pHooks->pShotHooks, pCtx->pRosters,
                           pUnit->pCell->nX, pUnit->pCell->nY, pUnit->pCell->nZ,
                           0, pCtx->apObjects[17], NULL, NULL, NULL);

        Unit_ApplyCellDamage(pUnit->pCell, 255, pCtx->apObjects[17]);
        Unit_ApplyCellDamage(pCtx->pCombat->pfnLookup(pUnit->pCell->nX + 1,
                             pUnit->pCell->nY, pUnit->pCell->nZ,
                             pCtx->pCombat->pUser), 255, pCtx->apObjects[17]);
        Unit_ApplyCellDamage(pCtx->pCombat->pfnLookup(pUnit->pCell->nX,
                             pUnit->pCell->nY + 1, pUnit->pCell->nZ,
                             pCtx->pCombat->pUser), 255, pCtx->apObjects[17]);
        Unit_ApplyCellDamage(pCtx->pCombat->pfnLookup(pUnit->pCell->nX + 1,
                             pUnit->pCell->nY + 1, pUnit->pCell->nZ,
                             pCtx->pCombat->pUser), 255, pCtx->apObjects[17]);
    }

    /* Spotted-flag sweep over a +-8 tile window on every layer, then a full Xcom
     * visibility refresh. */
    if (pCtx->bUiSlotArmed) {
        i32 nMaxX = 10 * (i32)pCtx->pCombat->pGeom->nDimX;
        i32 nMaxY = 10 * (i32)pCtx->pCombat->pGeom->nDimY;
        i32 nX1 = pUnit->pCell->nX + 8;
        i32 nX0 = pUnit->pCell->nX - 8;
        i32 nY1 = pUnit->pCell->nY + 8;
        i32 nY0 = pUnit->pCell->nY - 8;

        if (nX1 >= nMaxX) nX1 = nMaxX - 1;
        if (nY1 >= nMaxY) nY1 = nMaxY - 1;
        if (nX0 < 0) nX0 = 0;
        if (nY0 < 0) nY0 = 0;

        for (i32 nLayer = 0; nLayer < 4; ++nLayer) {
            for (i32 nY = nY0; nY <= nY1; ++nY) {
                for (i32 nX = nX0; nX <= nX1; ++nX) {
                    MapCell *pProbe = pCtx->pCombat->pfnLookup(nX, nY, nLayer,
                                                               pCtx->pCombat->pUser);
                    if (pProbe != NULL) {
                        pProbe->bFlags44 &= (u8)~1u;
                    }
                }
            }
        }
        UnitList_ForEachRefresh(pCtx->pRosters, pCtx->pVisCtx);
    }
}

/* The family nUnitType selects. The middle condition is the original's, precedence included:
 * t <= 8 || (t > 13 && (t <= 20 || t == 22)). */
static bool NameFamilyLow(u8 nUnitType)
{
    return nUnitType <= 3;
}

static bool NameFamilyPlain(u8 nUnitType)
{
    return nUnitType <= 8
        || (nUnitType > 13 && (nUnitType <= 20 || nUnitType == 22));
}

char *Unit_FormatDisplayName(Soldier *pUnit, i8 nMode,
                             const char *panRankLow, const char *panRankOther)
{
    const char *pszRank;

    if (NameFamilyLow(pUnit->nUnitType)) {
        pszRank = panRankLow + PUFO_RANK_NAME_STRIDE * (i32)pUnit->bRankType;
        if (nMode == 0) {
            pufo_strcpy(pUnit->szDisplayName, pszRank);
            pufo_strcat(pUnit->szDisplayName, ": ", sizeof(pUnit->szDisplayName));
            pufo_strcat(pUnit->szDisplayName, pUnit->szName, sizeof(pUnit->szDisplayName));
        } else if (nMode == 1) {
            pufo_strcpy(pUnit->szDisplayName, pszRank);
        } else if (nMode == 2) {
            pufo_strcpy(pUnit->szDisplayName, pUnit->szName);
        }
    } else if (NameFamilyPlain(pUnit->nUnitType)) {
        if (nMode == 0 || nMode == 2) {
            pufo_strcpy(pUnit->szDisplayName, pUnit->szName);
        } else {
            /* The original copies from 0x10DA38, the .bss zero page it also uses as "the empty
             * string". The effect is to clear the display name. */
            pUnit->szDisplayName[0] = '\0';
        }
    } else {
        pszRank = panRankOther + PUFO_RANK_NAME_STRIDE * (i32)pUnit->bRankType;
        if (nMode == 0) {
            pufo_strcpy(pUnit->szDisplayName, pUnit->szName);
            pufo_strcat(pUnit->szDisplayName, " - ", sizeof(pUnit->szDisplayName));
            pufo_strcat(pUnit->szDisplayName, pszRank, sizeof(pUnit->szDisplayName));
        } else if (nMode == 1) {
            pufo_strcpy(pUnit->szDisplayName, pUnit->szName);
        } else if (nMode == 2) {
            pufo_strcpy(pUnit->szDisplayName, pszRank);
        }
    }

    return pUnit->szDisplayName;
}
