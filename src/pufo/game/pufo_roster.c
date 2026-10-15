/* pufo_roster.c - the XCom unit roster and unit-selection navigation.
 *
 * Evidence: see pufo_roster.h for the address list and the verified policies.
 */
#include "game/pufo_roster.h"

#include "game/pufo_tactui.h"

#include <string.h>

i32 Roster_FindSelected(const UnitRoster *pRoster)
{
    i32 i;

    if (pRoster == NULL) {
        return 0;
    }
    /* Shared search loop, verified at 0x166B4-0x166DC: scan while the slot is not
     * the selected unit, so on a miss the index ends at nCount. */
    for (i = 0; i < pRoster->nCount; ++i) {
        if (pRoster->apUnits[i] == pRoster->pSelected) {
            break;
        }
    }
    return i;
}

i32 Roster_CycleNextIndex(UnitRoster *pRoster)
{
    i32 nFound;
    i32 nNext;

    if (pRoster == NULL || pRoster->nCount <= 0) {
        return -1;
    }
    nFound = Roster_FindSelected(pRoster);
    /* The original computes found+1 with no check that the search succeeded, so a
     * pSelUnit absent from the roster makes it read apXcomUnits[count+1]. The port
     * refuses instead of reading past the array (documented deviation). */
    if (nFound >= pRoster->nCount) {
        return -1;
    }
    nNext = nFound + 1;
    if (nNext == pRoster->nCount) {
        nNext = 0;
    }
    return nNext;
}

/* Shared by prev and next. `bForward` selects the direction; the caller-facing
 * functions below pass it. Returns -1 when no selectable unit exists (the port's
 * deviation for the original's non-terminating do-while). */
static i32 Roster_StepSelectable(const UnitRoster *pRoster, bool bForward)
{
    i32 nFound;
    i32 nSteps;

    if (pRoster == NULL || pRoster->nCount <= 0) {
        return -1;
    }
    nFound = Roster_FindSelected(pRoster);
    if (nFound >= pRoster->nCount) {
        nFound = 0;
    }

    /* The original is a do-while over the unit type, so it always takes at least
     * one step and never terminates if nothing is selectable. The step counter is
     * the port's safety bound: after a full lap the answer cannot change. */
    nSteps = 0;
    do {
        if (bForward) {
            nFound += 1;
            if (nFound == pRoster->nCount) {
                nFound = 0;
            }
        } else {
            if (nFound != 0) {
                nFound -= 1;
            } else {
                nFound = pRoster->nCount - 1;
            }
        }
        nSteps += 1;
        if (pRoster->apUnits[nFound] == NULL) {
            return -1;
        }
        /* The filter: skip every entry whose nUnitType exceeds 3. */
        if (pRoster->apUnits[nFound]->nUnitType <= PUFO_ROSTER_SELECTABLE_MAX_TYPE) {
            return nFound;
        }
        if (nSteps >= pRoster->nCount) {
            return -1; /* no selectable unit; the original would spin forever */
        }
    } while (true);
}

i32 Roster_PrevSelectableIndex(const UnitRoster *pRoster)
{
    return Roster_StepSelectable(pRoster, false);
}

i32 Roster_NextSelectableIndex(const UnitRoster *pRoster)
{
    return Roster_StepSelectable(pRoster, true);
}

i32 Roster_CycleNext(UnitRoster *pRoster, const RosterHooks *pHooks)
{
    i32 nIndex;

    if (pRoster == NULL || pHooks == NULL) {
        return -1;
    }
    if (pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(PUFO_ROSTER_SAMPLE, pHooks->pUser);
    }
    nIndex = Roster_CycleNextIndex(pRoster);
    if (nIndex < 0) {
        return -1;
    }
    /* CycleNext clears gap_2[1] BEFORE selecting, unlike prev/next. */
    pRoster->bActionLatch = 0;
    pRoster->pSelected = pRoster->apUnits[nIndex];
    if (pHooks->pfnSelectUnit != NULL) {
        pHooks->pfnSelectUnit(pRoster->pSelected, pHooks->pUser);
    }
    if (pHooks->pfnFocusSelected != NULL) {
        pHooks->pfnFocusSelected(pHooks->pUser);
    }
    return nIndex;
}

i32 Roster_SelectPrev(UnitRoster *pRoster, const RosterHooks *pHooks)
{
    i32 nIndex;

    if (pRoster == NULL || pHooks == NULL) {
        return -1;
    }
    if (pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(PUFO_ROSTER_SAMPLE, pHooks->pUser);
    }
    nIndex = Roster_PrevSelectableIndex(pRoster);
    if (nIndex < 0) {
        return -1;
    }
    pRoster->pSelected = pRoster->apUnits[nIndex];
    if (pHooks->pfnSelectUnit != NULL) {
        pHooks->pfnSelectUnit(pRoster->pSelected, pHooks->pUser);
    }
    if (pHooks->pfnFocusSelected != NULL) {
        pHooks->pfnFocusSelected(pHooks->pUser);
    }
    /* Prev/next end with Inv_BuildVisibleList; CycleNext does not. */
    Inv_BuildVisibleList(pHooks->pUi, pRoster->pSelected);
    return nIndex;
}

i32 Roster_SelectNext(UnitRoster *pRoster, const RosterHooks *pHooks)
{
    i32 nIndex;

    if (pRoster == NULL || pHooks == NULL) {
        return -1;
    }
    if (pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(PUFO_ROSTER_SAMPLE, pHooks->pUser);
    }
    nIndex = Roster_NextSelectableIndex(pRoster);
    if (nIndex < 0) {
        return -1;
    }
    pRoster->pSelected = pRoster->apUnits[nIndex];
    if (pHooks->pfnSelectUnit != NULL) {
        pHooks->pfnSelectUnit(pRoster->pSelected, pHooks->pUser);
    }
    if (pHooks->pfnFocusSelected != NULL) {
        pHooks->pfnFocusSelected(pHooks->pUser);
    }
    Inv_BuildVisibleList(pHooks->pUi, pRoster->pSelected);
    return nIndex;
}

/* --- the FILTERED variants ---------------------------------------------------
 *
 * Tact_SelectPrevUnitFiltered @ 0x19708 / Tact_SelectNextUnitFiltered @ 0x197b8.
 *
 * The extra filter term is `bFilterActive`: a non-human (nUnitType > 3) is skipped
 * only while it is set. When it is clear the first non-human encountered is
 * accepted, so the scan stops there instead of walking to another human.
 *
 * The next variant also re-reads the count inside the loop and selects/focuses once
 * per examined candidate, which is a visible behaviour and is reproduced below.
 */

/* Shared candidate walk. `bForward` picks the direction. The callback reports
 * whether the candidate at `nIndex` is ACCEPTABLE, which encodes the two-term
 * filter difference in one place. Returns -1 when nothing is acceptable.
 *
 * One context pointer is used for both callbacks, because they need overlapping
 * data; splitting it into two would be the same information stored twice. */
typedef struct FilterWalkCtx {
    bool                     bFilterActive; /* the second filter term */
    const RosterFilterHooks *pHooks;        /* for the per-candidate visit */
} FilterWalkCtx;

static i32 Roster_WalkFiltered(const UnitRoster *pRoster, bool bForward,
                               bool (*pfnAccept)(const UnitRoster *p, i32 nIndex,
                                                 void *pUser),
                               void (*pfnVisit)(const UnitRoster *p, i32 nIndex,
                                                void *pUser),
                               void *pUser)
{
    i32 nFound;
    i32 nSteps;

    if (pRoster == NULL || pRoster->nCount <= 0) {
        return -1;
    }
    nFound = Roster_FindSelected(pRoster);
    if (nFound >= pRoster->nCount) {
        nFound = 0;
    }

    /* A do-while in the original: at least one step, and it never terminates if no
     * candidate is acceptable. The step counter is the port's safety bound. */
    nSteps = 0;
    do {
        if (bForward) {
            nFound += 1;
            if (nFound == pRoster->nCount) {
                nFound = 0;
            }
        } else {
            if (nFound != 0) {
                nFound -= 1;
            } else {
                nFound = pRoster->nCount - 1;
            }
        }
        nSteps += 1;
        if (pRoster->apUnits[nFound] == NULL) {
            return -1;
        }
        /* The next variant acts on every candidate it examines, before deciding
         * whether to stop. */
        if (pfnVisit != NULL) {
            pfnVisit(pRoster, nFound, pUser);
        }
        if (pfnAccept != NULL && pfnAccept(pRoster, nFound, pUser)) {
            return nFound;
        }
        if (nSteps >= pRoster->nCount) {
            return -1; /* nothing acceptable; the original would spin forever */
        }
    } while (true);
}

/* The two-term filter: a human is always acceptable; a non-human only when the
 * filter flag is clear. */
static bool Filter_Accept(const UnitRoster *pRoster, i32 nIndex, void *pUser)
{
    const FilterWalkCtx *pCtx = (const FilterWalkCtx *)pUser;

    if (pRoster->apUnits[nIndex]->nUnitType <= PUFO_ROSTER_SELECTABLE_MAX_TYPE) {
        return true;
    }
    /* Non-human: acceptable only when the filter is off. */
    return !pCtx->bFilterActive;
}

/* The per-candidate visit the next variant performs: select and focus every
 * candidate it examines, before the filter decides whether to stop. */
static void NextFiltered_Visit(const UnitRoster *pRoster, i32 nIndex, void *pUser)
{
    const FilterWalkCtx *pCtx = (const FilterWalkCtx *)pUser;
    const RosterFilterHooks *pHooks = pCtx->pHooks;

    if (pHooks->base.pfnSelectUnit != NULL) {
        pHooks->base.pfnSelectUnit(pRoster->apUnits[nIndex], pHooks->base.pUser);
    }
    if (pHooks->base.pfnFocusSelected != NULL) {
        pHooks->base.pfnFocusSelected(pHooks->base.pUser);
    }
}

i32 Roster_SelectPrevFiltered(UnitRoster *pRoster, const RosterFilterHooks *pHooks)
{
    FilterWalkCtx ctx;
    i32 nIndex;

    if (pRoster == NULL || pHooks == NULL) {
        return -1;
    }
    if (pHooks->base.pfnPlaySample != NULL) {
        pHooks->base.pfnPlaySample(PUFO_ROSTER_SAMPLE, pHooks->base.pUser);
    }

    ctx.bFilterActive = (pHooks->pfnIsFilterActive != NULL)
                            ? pHooks->pfnIsFilterActive(pHooks->pFilterUser)
                            : false;
    ctx.pHooks = pHooks;

    /* The prev variant's guard tests both terms and does NOT act per candidate, so
     * no visit callback is passed. */
    nIndex = Roster_WalkFiltered(pRoster, false, Filter_Accept, NULL, &ctx);
    if (nIndex < 0) {
        return -1;
    }
    pRoster->pSelected = pRoster->apUnits[nIndex];
    if (pHooks->base.pfnSelectUnit != NULL) {
        pHooks->base.pfnSelectUnit(pRoster->pSelected, pHooks->base.pUser);
    }
    if (pHooks->base.pfnFocusSelected != NULL) {
        pHooks->base.pfnFocusSelected(pHooks->base.pUser);
    }
    Inv_BuildVisibleList(pHooks->base.pUi, pRoster->pSelected);
    return nIndex;
}

i32 Roster_SelectNextFiltered(UnitRoster *pRoster, const RosterFilterHooks *pHooks)
{
    FilterWalkCtx ctx;
    i32 nIndex;

    if (pRoster == NULL || pHooks == NULL) {
        return -1;
    }
    if (pHooks->base.pfnPlaySample != NULL) {
        pHooks->base.pfnPlaySample(PUFO_ROSTER_SAMPLE, pHooks->base.pUser);
    }

    ctx.bFilterActive = (pHooks->pfnIsFilterActive != NULL)
                            ? pHooks->pfnIsFilterActive(pHooks->pFilterUser)
                            : false;
    ctx.pHooks = pHooks;

    nIndex = Roster_WalkFiltered(pRoster, true, Filter_Accept, NextFiltered_Visit,
                                 &ctx);
    if (nIndex < 0) {
        return -1;
    }
    /* The final candidate was already selected by the visit callback, so only the
     * visible-list rebuild remains -- the original calls it after the loop. */
    pRoster->pSelected = pRoster->apUnits[nIndex];
    Inv_BuildVisibleList(pHooks->base.pUi, pRoster->pSelected);
    return nIndex;
}

/* --------------------------------------------------------- side-list membership */

/* Removes the entry at `nIndex` from a pointer list, compacting downward, and
 * decrements the count. Mirrors Tact_RemoveUnitFromSideLists' compaction: the copy
 * loop only runs while `i < count - 1`, so a hit at the last index just drops the
 * count. */
static void SideLists_CompactAt(Soldier **apUnits, i32 *pnCount, i32 nIndex)
{
    if (nIndex < 0 || nIndex >= *pnCount) {
        return;
    }
    if (nIndex < *pnCount - 1) {
        for (i32 i = nIndex; i < *pnCount - 1; ++i) {
            apUnits[i] = apUnits[i + 1];
        }
    }
    *pnCount -= 1;
}

/* The shared search: returns the index of pUnit, or -1 when absent. */
static i32 SideLists_Find(Soldier *const *apUnits, i32 nCount, const Soldier *pUnit)
{
    for (i32 i = 0; i < nCount; ++i) {
        if (apUnits[i] == pUnit) {
            return i;
        }
    }
    return -1;
}

bool SideLists_RemoveUnit(SideLists *pLists, Soldier *pUnit)
{
    i32 nIndex;

    if (pLists == NULL || pUnit == NULL) {
        return false;
    }

    /* The XCom list is searched FIRST, and the enemy list only when the unit was
     * not found there -- so a unit present in both is removed from XCom only. */
    nIndex = SideLists_Find(pLists->apXcom, pLists->nXcom, pUnit);
    if (nIndex >= 0) {
        SideLists_CompactAt(pLists->apXcom, &pLists->nXcom, nIndex);
        return true;
    }

    nIndex = SideLists_Find(pLists->apEnemy, pLists->nEnemy, pUnit);
    if (nIndex >= 0) {
        SideLists_CompactAt(pLists->apEnemy, &pLists->nEnemy, nIndex);
        return true;
    }
    return false;
}

/* ----------------------------------------------------------- soldier-list pass */

i32 SideLists_MarkAllSoldiersSeen(Soldier **apSoldiers, i32 nSoldierCount)
{
    i32 nMarked = 0;

    if (apSoldiers == NULL || nSoldierCount <= 0) {
        return 0;
    }
    for (i32 i = 0; i < nSoldierCount; ++i) {
        Soldier *pSoldier = apSoldiers[i];

        if (pSoldier == NULL) {
            continue;
        }
        /* Both conditions must hold: a live link AND non-zero HP. A dead soldier's
         * link is left in place -- the original does not clean it up. */
        if (pSoldier->pLinked1040 != NULL && pSoldier->uHp != 0) {
            /* The original increments the u16 at pLinked1040 + 250. The port keeps
             * that raw offset because the linked structure is not modelled yet. */
            u16 *puCounter = (u16 *)((u8 *)pSoldier->pLinked1040 + 250);

            *puCounter = (u16)(*puCounter + 1);
            pSoldier->pLinked1040 = NULL;
            nMarked += 1;
        }
    }
    return nMarked;
}

/* ------------------------------------------------------------ soldier searches */

/* The three conditions both searches share, ANDed. */
static bool Soldier_MatchesSmall(const Soldier *pUnit, u8 nType)
{
    if (pUnit == NULL) {
        return false;
    }
    if (pUnit->bRankType != nType) {
        return false;
    }
    /* uHp != 0 is the "alive" test. */
    if (pUnit->uHp == 0) {
        return false;
    }
    /* The tank-sized unit types are excluded, so a rank match alone is not enough. */
    return !Unit_bIsLarge(pUnit);
}

i32 Soldier_FindNextSmallByType(Soldier *const *apSoldiers, i32 nSoldierCount,
                                u8 nType, i32 nStart)
{
    if (apSoldiers == NULL || nSoldierCount <= 0) {
        return -1;
    }
    /* An nStart at or past the count fails immediately, which is what bounds the
     * original's loop. */
    if (nStart < 0 || nStart >= nSoldierCount) {
        return -1;
    }
    for (i32 i = nStart; i < nSoldierCount; ++i) {
        if (Soldier_MatchesSmall(apSoldiers[i], nType)) {
            return i;
        }
    }
    return -1;
}

i32 Soldier_CountSmallByType(Soldier *const *apSoldiers, i32 nSoldierCount, u8 nType)
{
    i32 nCount = 0;

    if (apSoldiers == NULL || nSoldierCount <= 0) {
        return 0;
    }
    for (i32 i = 0; i < nSoldierCount; ++i) {
        if (Soldier_MatchesSmall(apSoldiers[i], nType)) {
            nCount += 1;
        }
    }
    return nCount;
}

/* Soldier_PickBestScoreAmong @ 0x46fe0. */
static i32 Soldier_MissionScore(const Soldier *pSoldier)
{
    return 5 * ((i32)pSoldier->uKills + 2 * (i32)pSoldier->uMissions);
}

i32 Soldier_PickBestScoreAmong(Soldier *const *apSoldiers, const i32 anIdx[8],
                               i32 *pnBestScore, i32 *pnBestIndex)
{
    i32 nBest = 0;
    i32 nBestIdx = 0;

    if (apSoldiers != NULL && anIdx[0] != -1 && apSoldiers[anIdx[0]] != NULL) {
        nBest = Soldier_MissionScore(apSoldiers[anIdx[0]]);
    }
    for (i32 i = 1; i < 8; ++i) {
        i32 nScore;

        if (anIdx[i] == -1 || apSoldiers[anIdx[i]] == NULL) {
            continue;
        }
        nScore = Soldier_MissionScore(apSoldiers[anIdx[i]]);
        if (nBest < nScore) {
            nBest = nScore;
            nBestIdx = i;
        }
    }
    if (pnBestScore != NULL) {
        *pnBestScore = nBest;
    }
    if (pnBestIndex != NULL) {
        *pnBestIndex = nBestIdx;
    }
    return nBest;
}

/* --- Soldier_PromoteByTypeScores @ 0x47280 --------------------------------- */

#define PROMOTE_SCORE_ENGINEER    375 /* MOV R1,#0x177 at 0x47318 */
#define PROMOTE_SCORE_LEADER      270 /* MOV R2,#0x10E at 0x473F0 */
#define PROMOTE_SCORE_NAVIGATOR   125 /* CMP  R0,#0x7D  at 0x474B4 */
#define PROMOTE_SCORE_SOLDIER      90 /* CMP  R2,#0x5A  at 0x475F8 */
#define PROMOTE_SCORE_ROOKIE       45 /* CMP  R8,#0x2D  at 0x4768C */

/* The two scratch values the ladder shares with Soldier_PickBestScoreAmong. The original
 * keeps them in globals (nPromoteBestScore @ 0x15880C, nPromoteBestPick @ 0x158810) that
 * nothing outside these two functions reads, so the port keeps them here. */
typedef struct PromoteScratch {
    i32 nBestScore;
    i32 nBestPick;
} PromoteScratch;

/* One "best of two" rung: the two lowest-indexed live non-large soldiers of nFromRank are
 * compared by mission score and the better one is promoted if the score clears the rung's
 * threshold. bEmptyTop picks the engineer rung's `count(to) == 0` test out of the other two
 * rungs' `count(to) < 2`. */
static void Promote_BestOfTwo(const SoldierPromoteEnv *pEnv, PromoteScratch *pScr,
                              i32 nFromRank, i32 nToRank, bool bEmptyTop, i32 nThreshold)
{
    Soldier **apSoldiers = pEnv->apSoldiers;
    const i32 nCount = (i32)*pEnv->pnSoldierCount;
    const i32 nAtTop = Soldier_CountSmallByType(apSoldiers, nCount, (u8)nToRank);
    i32 anIdx[8];
    i32 nA;
    i32 nB;
    i32 i;

    if (bEmptyTop ? (nAtTop != 0) : (nAtTop >= 2)) {
        return;
    }
    if (Soldier_CountSmallByType(apSoldiers, nCount, (u8)nFromRank) < 2) {
        return;
    }
    nA = Soldier_FindNextSmallByType(apSoldiers, nCount, (u8)nFromRank, 0);
    nB = Soldier_FindNextSmallByType(apSoldiers, nCount, (u8)nFromRank, nA + 1);
    anIdx[0] = nA;
    anIdx[1] = nB;
    for (i = 2; i < 8; ++i) {
        anIdx[i] = -1;
    }
    Soldier_PickBestScoreAmong(apSoldiers, anIdx, &pScr->nBestScore, &pScr->nBestPick);

    /* 0x47308: pick 0 means candidate A won, pick 1 means candidate B won. With pick 0 and the
     * score short of the threshold the original jumps the whole rung, candidate B included. */
    if (pScr->nBestPick == 0) {
        if (pScr->nBestScore < nThreshold) {
            return;
        }
        apSoldiers[nA]->bRankType = (u8)nToRank;   /* 0x47338 */
        apSoldiers[nA]->bRankDirty = 1;            /* 0x47340 */
    } else if (pScr->nBestPick == 1 && pScr->nBestScore >= nThreshold) {
        apSoldiers[nB]->bRankType = (u8)nToRank;   /* 0x47368 */
        apSoldiers[nB]->bRankDirty = 1;            /* 0x47370 */
    }
}

/* 0x47504: rank 1 -> rank 2, count(1) / 3 attempts, skipped outright when the roster already
 * holds at least that many rank-2 soldiers. Unlike the rungs above, the two scans here test
 * bRankType alone -- no alive test and no large-unit test. */
static void Promote_Soldiers(const SoldierPromoteEnv *pEnv, PromoteScratch *pScr)
{
    Soldier **apSoldiers = pEnv->apSoldiers;
    const i32 nCount = (i32)*pEnv->pnSoldierCount;
    const i32 nAttempts = Soldier_CountSmallByType(apSoldiers, nCount, 1) / 3;
    i32 nScore;
    i32 i;

    if (Soldier_CountSmallByType(apSoldiers, nCount, 2) >= nAttempts || nAttempts <= 0) {
        return;
    }

    /* 0x47548 seeds the running score from nPromoteBestPick -- the previous rung's 0/1 pick,
     * not from a score. Harmless (0 and 1 sit below every real score and below the 90 gate)
     * but reproduced. */
    nScore = pScr->nBestPick;

    for (i = nAttempts; i > 0; --i) {
        i32 k;

        /* 0x47570: a first scan that overwrites both scratch values for EVERY rank-1 soldier,
         * so it leaves the LAST one as the seed for the real max scan below. Reproduced. */
        for (k = 0; k < nCount; ++k) {
            if (apSoldiers[k]->bRankType == 1) {
                nScore = Soldier_MissionScore(apSoldiers[k]);
                pScr->nBestScore = k;
                pScr->nBestPick = nScore;
            }
        }
        /* 0x475B4: the max scan. */
        for (k = 0; k < nCount; ++k) {
            i32 nCand;

            if (apSoldiers[k]->bRankType != 1) {
                continue;
            }
            nCand = Soldier_MissionScore(apSoldiers[k]);
            if (nScore < nCand) {
                nScore = nCand;
                pScr->nBestScore = k;
                pScr->nBestPick = nCand;
            }
        }
        if (nScore >= PROMOTE_SCORE_SOLDIER) {
            apSoldiers[pScr->nBestScore]->bRankType = 2;   /* 0x47610 */
            apSoldiers[pScr->nBestScore]->bRankDirty = 1;  /* 0x47620 */
        }
        nScore = pScr->nBestPick;                           /* 0x47624 */
    }
}

/* 0x47654: the bottom rung has no gate at all -- every rank-0 soldier whose score is above 45
 * and that is still standing becomes rank 1. */
static void Promote_Rookies(const SoldierPromoteEnv *pEnv)
{
    Soldier **apSoldiers = pEnv->apSoldiers;
    const i32 nCount = (i32)*pEnv->pnSoldierCount;
    i32 k;

    for (k = 0; k < nCount; ++k) {
        Soldier *pUnit = apSoldiers[k];

        if (pUnit->bRankType != 0) {
            continue;
        }
        if (Soldier_MissionScore(pUnit) <= PROMOTE_SCORE_ROOKIE) {
            continue;
        }
        if (pUnit->uHp == 0) {
            continue;
        }
        pUnit->bRankType = 1;      /* 0x47698 */
        pUnit->bRankDirty = 1;     /* 0x476A0 */
    }
}

/* Soldier_PromoteByTypeScores @ 0x47280. The rungs run from the top one down. */
void Soldier_PromoteByTypeScores(const SoldierPromoteEnv *pEnv)
{
    PromoteScratch scr;

    scr.nBestScore = 0;
    scr.nBestPick = 0;

    Promote_BestOfTwo(pEnv, &scr, 4, 5, true,  PROMOTE_SCORE_ENGINEER);  /* 0x472C0 */
    Promote_BestOfTwo(pEnv, &scr, 3, 4, false, PROMOTE_SCORE_LEADER);    /* 0x47374 */
    Promote_BestOfTwo(pEnv, &scr, 2, 3, false, PROMOTE_SCORE_NAVIGATOR); /* 0x47440 */
    Promote_Soldiers(pEnv, &scr);                                        /* 0x47504 */
    Promote_Rookies(pEnv);                                               /* 0x47654 */
}
