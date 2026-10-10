/* pufo_unit.c - soldier entities: identity, state predicates, placement, facing.
 *
 * Evidence: see pufo_unit.h for the full address list.
 */
#include "game/pufo_unit.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game/pufo_los.h"
#include "game/pufo_combat.h"   /* UnitRosters */
#include "game/pufo_vistry.h"   /* VisRay, ray-table counts */
#include "game/pufo_tactctl.h"   /* mission type ids */
#include "game/pufo_boot.h"   /* the extracted name tables, and TextTables_* */
#include "game/pufo_font.h"   /* Font_GetSprByIndex / g_fontSys */
#include "game/pufo_path.h"   /* g_aStepDir */
#include "util/pufo_rand.h"   /* Game_RandU16 */
#include "util/pufo_str.h"    /* pufo_strncpy / pufo_strcat */

/* byte_A7B80 @ 0xa7b80, stride 4: byte[+0] is "flag 0" and byte[+1] the "is large"
 * flag. Both columns decoded from the 64 bytes at that address. */
const u8 g_aUnitTypeIsLarge[PUFO_UNIT_TYPE_COUNT] = {
    0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 1, 0, 1, 0, 0
};
const u8 g_aUnitTypeFlag0[PUFO_UNIT_TYPE_COUNT] = {
    0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0
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

/* Soldier_RemoveById @ 0x36b8c, compaction half.
 *
 * The original also searches by id (Soldier.nUnk440 @ 0x1b8); the search belongs
 * to the caller here so this module does not need the whole struct. Both
 * memory-safety deviations from the original are documented in the header. */
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

/* --- Tact_UpdateUnitControl @ 0x4a444, the unit-arithmetic half --- */

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

bool Unit_CanStepExitDir(const Soldier *pUnit, i32 nDir, const MapGeom *pGeom,
                         MapCellLookup pfnLookup, void *pUser)
{
    const StepDir *pStep;
    i32 nX;
    i32 nY;
    i32 nZ;
    bool bBlocked = false;

    if (pUnit == NULL || pGeom == NULL || pfnLookup == NULL
        || nDir < 0 || nDir > 7) {
        return false;
    }
    pStep = &g_aStepDir[nDir];
    nX = (i32)pStep->dx + (i32)pUnit->nMultiTarget;
    nY = (i32)(u8)pUnit->gap_1B + (i32)pStep->dy;
    nZ = (i32)pUnit->multiOrAnim.anim.nPathZ;
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
