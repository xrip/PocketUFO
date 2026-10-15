/* pufo_roster.h - the XCom unit roster and the unit-selection navigation.
 *
 * Evidence: Tact_CycleNextUnit @ 0x16684, Tact_SelectPrevUnit @ 0x17260,
 *           Tact_SelectNextUnit @ 0x17304, Tact_SelectUnit (0x2c6c4, not ported),
 *           MapView_FocusSelected @ 0x16710.
 *
 * ## Roster fields
 *
 * All three navigation functions walk the XCom unit array to find the currently
 * selected unit, then advance. The fields come from the `Map` type:
 *
 *     apXcomUnits     @ 0x15154, 120 bytes = 30 pointers
 *     nXcomUnitCount  @ 0x1511f, one byte
 *     pSelUnit        @ 0x15334
 *     gap_2[1]        @ 0x00002, the large-unit cursor-suppression byte
 *
 * All three share this search loop, verified in the disassembly of
 * `Tact_CycleNextUnit` at 0x166B4-0x166DC: scan i = 0..count-1 comparing
 * `apXcomUnits[i]` against `pSelUnit`. On a hit the index is the found slot; on a
 * miss `v0` ends at `count`.
 *
 * ## The three advance policies
 *
 *   CycleNext   : v2 = found + 1; if (count == v2) v2 = 0; select v2.
 *                 NO selectable-unit filter -- it advances to whatever is next.
 *                 Also clears gap_2[1] before selecting.
 *   SelectPrev  : do { found = (found != 0) ? found - 1 : count - 1; }
 *                 while (apXcomUnits[found]->nUnitType > 3);
 *   SelectNext  : do { if (count == ++found) found = 0; }
 *                 while (apXcomUnits[found]->nUnitType > 3);
 *
 * The prev/next filter skips any unit whose `nUnitType` exceeds 3, i.e. every
 * entry that is not a human soldier type. It is a **do-while**, so at least one
 * step always happens.
 *
 * ## Reproduced quirks
 *
 * - `CycleNext` computes `found + 1` without checking whether the search found
 *   anything, so when `pSelUnit` is not in the array (`found == count`) it reads
 *   `apXcomUnits[count + 1]` -- out of bounds. Reproduced as a documented
 *   deviation: the port returns -1 instead of reading past the array.
 * - The prev/next do-while cannot terminate if no roster entry has
 *   `nUnitType <= 3`. Reproduced as a documented deviation: the port stops after
 *   `nCount` steps rather than spinning forever, because a hang is strictly worse
 *   than the original's undefined behaviour.
 */
#ifndef PUFO_ROSTER_H
#define PUFO_ROSTER_H

#include "game/pufo_unit.h"
#include "pufo_common.h"

/* The UI state whose ground-item list prev/next rebuild. Defined in
 * pufo_tactui.h; only a pointer is used here. */
typedef struct TactUiState TactUiState;

/* apXcomUnits is 120 bytes on the original = 30 pointers. */
#define PUFO_ROSTER_MAX 30

/* The largest unit type the prev/next filter accepts. Types 0..3 are the human
 * soldier kinds; everything above is skipped. */
#define PUFO_ROSTER_SELECTABLE_MAX_TYPE 3

typedef struct UnitRoster {
    Soldier *apUnits[PUFO_ROSTER_MAX]; /* apXcomUnits @ 0x15154 */
    i32      nCount;                   /* nXcomUnitCount @ 0x1511f */
    Soldier *pSelected;                /* pSelUnit @ 0x15334 */
    u8       bActionLatch;     /* gap_2[1] @ 0x00002, cleared by CycleNext */
} UnitRoster;

/* The shared search. Returns the index of `pSelected`, or `nCount` when it is not
 * in the array (the original's miss state). */
i32 Roster_FindSelected(const UnitRoster *pRoster);

/* The three advance policies. Each returns the index to select, or -1 when the
 * roster cannot advance (the port's documented deviation for the original's
 * out-of-bounds read and its non-terminating scan). */
i32 Roster_CycleNextIndex(UnitRoster *pRoster);
i32 Roster_PrevSelectableIndex(const UnitRoster *pRoster);
i32 Roster_NextSelectableIndex(const UnitRoster *pRoster);

/* What the navigation functions need from the world. `Tact_SelectUnit` (0x2c6c4, not ported)
 * and `MapView_FocusSelected` @ 0x16710 are injected because both need the map,
 * the status line and the inventory list; `pfnPlaySample` stands in for
 * `Sound_Play` so the roster layer stays free of an audio dependency. */
typedef struct RosterHooks {
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    void (*pfnSelectUnit)(Soldier *pUnit, void *pUser);
    void (*pfnFocusSelected)(void *pUser);          /* MapView_FocusSelected */
    /* Map.pUi half: prev/next end with Inv_BuildVisibleList @ 0x2d580 on this
     * state, using pRoster->pSelected as the unit. NULL skips the rebuild. */
    TactUiState *pUi;
    void *pUser;
} RosterHooks;

/* The sample index all three functions play first. */
#define PUFO_ROSTER_SAMPLE 17

/* Tact_CycleNextUnit @ 0x16684. Plays the sample, advances one slot with wrap,
 * clears the large-unit cursor-suppression byte, selects, and focuses. Returns the
 * selected index, or -1 on the documented miss deviation. */
i32 Roster_CycleNext(UnitRoster *pRoster, const RosterHooks *pHooks);

/* Tact_SelectPrevUnit @ 0x17260. Advances backwards to the previous selectable
 * unit, selects, focuses, then rebuilds the visible inventory list. */
i32 Roster_SelectPrev(UnitRoster *pRoster, const RosterHooks *pHooks);

/* Tact_SelectNextUnit @ 0x17304. Same as SelectPrev but forwards. */
i32 Roster_SelectNext(UnitRoster *pRoster, const RosterHooks *pHooks);

/* --- the FILTERED variants --------------------------------------------------
 *
 * Tact_SelectPrevUnitFiltered @ 0x19708 and Tact_SelectNextUnitFiltered @ 0x197b8
 * look like the pair above but differ in two ways that matter:
 *
 *   1. They add a SECOND filter term: a non-human (nUnitType > 3) is skipped only
 *      while LOBYTE(apUiSlotsA[8]) != 0. So when that byte is clear, the very first
 *      non-human encountered is ACCEPTED rather than skipped.
 *
 *   2. The NEXT variant re-reads nXcomUnitCount from the context inside the loop
 *      (`nXcomUnitCount = pCtxBase.nXcomUnitCount`), and it calls Tact_SelectUnit
 *      and MapView_FocusSelected INSIDE the loop, once per candidate examined. So a
 *      next-selection that walks past three non-humans selects three units in turn
 *      and paints three times before settling. That is a visible behaviour, not an
 *      optimisation detail, and it is reproduced.
 *
 * The prev variant tests both conditions in one while guard, so it selects once.
 *
 * Returned exactly like the unfiltered pair, with the port's step bound standing in
 * for the original's non-terminating scan. */
typedef struct RosterFilterHooks {
    RosterHooks base;
    /* LOBYTE(apUiSlotsA[8]) != 0, the flag that keeps non-humans filtered out. */
    bool (*pfnIsFilterActive)(void *pUser);
    void *pFilterUser;
} RosterFilterHooks;

i32 Roster_SelectPrevFiltered(UnitRoster *pRoster, const RosterFilterHooks *pHooks);
i32 Roster_SelectNextFiltered(UnitRoster *pRoster, const RosterFilterHooks *pHooks);

/* --- side-list membership ---------------------------------------------------
 *
 * Tact_RemoveUnitFromSideLists @ 0x33148. Field offsets, resolved against `Map`:
 *
 *     apXcomUnits    @ 0x15154   nXcomUnitCount   @ 0x1511F (u8)
 *     apEnemyUnits   @ 0x151CC   nEnemyUnitCount  @ 0x15124 (u8)
 *
 * It searches the XCom list first and, only when the unit is not in it, the enemy
 * list. On a hit at index i it compacts by copying entries i+1.. down one slot and
 * decrements that list's count. A miss in both lists changes nothing.
 *
 * The original's compaction guard is `i < count - 1`, so a hit at the LAST index
 * skips the copy loop and only the count is decremented -- correct, because there
 * is nothing above it to move. */
typedef struct SideLists {
    Soldier **apXcom;   /* apXcomUnits  @ 0x15154 */
    i32       nXcom;    /* nXcomUnitCount @ 0x1511F */
    Soldier **apEnemy;  /* apEnemyUnits @ 0x151CC */
    i32       nEnemy;   /* nEnemyUnitCount @ 0x15124 */
} SideLists;

/* Returns true when the unit was found and removed. */
bool SideLists_RemoveUnit(SideLists *pLists, Soldier *pUnit);

/* --- soldier-list pass ------------------------------------------------------
 *
 * Tact_MarkAllSoldiersSeen @ 0x33bf0. Walks every soldier in `apSoldiers` and, for
 * each one that has a `pLinked1040` link AND non-zero `uHp`, increments the u16 at
 * `pLinked1040 + 250` and clears the link.
 *
 * Note the count is reloaded from the context inside the loop in the original (its
 * `nSoldierCount = pCtxBase.nSoldierCount;` inside the branch); the port reads it
 * once because nothing in this function changes it. `apSoldiers` is `Soldier*[512]`
 * by size (2048 bytes) and `nSoldierCount` is a u16 (IDA reads it with LOWORD).
 *
 * Returns the number of soldiers whose link was cleared. */
#define PUFO_SOLDIER_LIST_MAX 512

i32 SideLists_MarkAllSoldiersSeen(Soldier **apSoldiers, i32 nSoldierCount);

/* --- soldier searches -------------------------------------------------------
 *
 * Two searches over the soldier list that filter on rank, HP and size. Both are
 * used by the alien-picking code, which wants SMALL units of a given rank.
 *
 * The three conditions are identical in both functions and are ANDed:
 *
 *     bRankType == nType  &&  uHp != 0  &&  !Unit_bIsLarge(pUnit)
 *
 * `uHp != 0` is the "alive" test, and `!bIsLarge` excludes the tank-sized unit
 * types, so a type match alone is not enough.
 */

/* Soldier_FindNextSmallByType @ 0x47164.
 *
 * Returns the index of the first matching soldier at or after `nStart`, or -1 when
 * there is none. An `nStart` at or past the count returns -1 immediately, so the
 * scan is always bounded by the live count.
 *
 * The original reloads `nSoldierCount` from the context inside the loop; the port
 * reads it once because nothing here changes it. */
i32 Soldier_FindNextSmallByType(Soldier *const *apSoldiers, i32 nSoldierCount,
                                u8 nType, i32 nStart);

/* Soldier_CountSmallByType @ 0x471f8: how many soldiers match the three conditions. */
i32 Soldier_CountSmallByType(Soldier *const *apSoldiers, i32 nSoldierCount, u8 nType);

/* Soldier_PickBestScoreAmong @ 0x46fe0.
 *
 * Scores each of eight roster slots as `5 * (uKills + 2 * uMissions)` and returns
 * the highest, storing the winning score and the winning slot's POSITION (0..7)
 * through the two output pointers (the original's `nPromoteBestScore` @ 0x15880C and
 * `nPromoteBestPick` @ 0x158810). A slot index of -1 is skipped. The original leaves
 * the score uninitialised when slot 0 is -1; the port starts from 0 and documents
 * that deviation. */
i32 Soldier_PickBestScoreAmong(Soldier *const *apSoldiers, const i32 anIdx[8],
                               i32 *pnBestScore, i32 *pnBestIndex);

/* --- the post-mission rank ladder ------------------------------------------
 *
 * Soldier_PromoteByTypeScores @ 0x47280 (1,096 B). One step up per rung, run from the
 * top rung down, each rung needing a live, non-large soldier of the rank below
 * (Soldier_CountSmallByType / Soldier_FindNextSmallByType) and a high enough mission
 * score `5 * (uKills + 2 * uMissions)`:
 *
 *   4 -> 5  count(5) == 0 (NOT < 2) and count(4) >= 2, best of two, score >= 375  0x472C0
 *   3 -> 4  count(4) <  2 and count(3) >= 2, best of two, score >= 270            0x47374
 *   2 -> 3  count(3) <  2 and count(2) >= 2, best of two, score >= 125            0x47440
 *   1 -> 2  count(1) / 3 attempts, each promoting the best rank-1 soldier with
 *           score >= 90, while count(2) < that attempt count                      0x47504
 *   0 -> 1  every rank-0 soldier with score > 45 and uHp != 0, no gate            0x47664
 *
 * The engineer rung tests `count(5) == 0` while the two below it test `< 2`: with one
 * engineer already on the roster no second engineer is ever promoted. Reproduced.
 *
 * The two scratch globals this shares with Soldier_PickBestScoreAmong have no reader
 * outside these two functions (16 xrefs each, all inside them), so the port keeps them
 * as locals. Note the original uses them with SWAPPED meanings between the rungs: the
 * three "best of two" rungs store the score in nPromoteBestScore and the 0/1 candidate
 * pick in nPromoteBestPick, while the 1 -> 2 rung stores the candidate INDEX in
 * nPromoteBestScore and the score in nPromoteBestPick -- and it seeds its running score
 * from nPromoteBestPick, i.e. from the previous rung's 0/1 pick, not from a score
 * (0x47548). Reproduced; it is harmless because 0 and 1 are below every real score and
 * below the 90 gate.
 */
typedef struct SoldierPromoteEnv {
    Soldier **apSoldiers;        /* Map_apSoldiers @ Map+0x1198 */
    const u16 *pnSoldierCount;   /* nSoldierCount @ 0x115350 */
} SoldierPromoteEnv;

void Soldier_PromoteByTypeScores(const SoldierPromoteEnv *pEnv);

#endif /* PUFO_ROSTER_H */
