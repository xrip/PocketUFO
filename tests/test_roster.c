/* test_roster.c - checks the XCom roster navigation policies.
 *
 * The three policies are transcribed from the decompilation; the tests pin the
 * exact wrap arithmetic and the nUnitType > 3 filter that distinguishes them.
 */
#include "game/pufo_roster.h"

#include <stdio.h>
#include <string.h>

static int g_nFailures;

static void Check(bool bCond, const char *pszWhat)
{
    if (bCond) {
        printf("  ok   %s\n", pszWhat);
    } else {
        printf("  FAIL %s\n", pszWhat);
        g_nFailures += 1;
    }
}

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

/* --- hook recording ------------------------------------------------------- */

typedef struct HookLog {
    i32      nSamples;
    i32      nLastSample;
    i32      nSelects;
    Soldier *pLastSelected;
    i32      nFocus;
    i32      nRebuild;
} HookLog;

static HookLog g_log;

static void OnPlaySample(i32 nSample, void *pUser)
{
    (void)pUser;
    g_log.nSamples += 1;
    g_log.nLastSample = nSample;
}

static void OnSelectUnit(Soldier *pUnit, void *pUser)
{
    (void)pUser;
    g_log.nSelects += 1;
    g_log.pLastSelected = pUnit;
}

static void OnFocusSelected(void *pUser)
{
    (void)pUser;
    g_log.nFocus += 1;
}

static void OnRebuildVisibleList(void *pUser)
{
    (void)pUser;
    g_log.nRebuild += 1;
}

static const RosterHooks g_hooks = {
    OnPlaySample, OnSelectUnit, OnFocusSelected, OnRebuildVisibleList, NULL
};

static void LogReset(void)
{
    memset(&g_log, 0, sizeof(g_log));
}

/* Builds a roster of nCount soldiers, all human (type 0) unless nFirstAlien
 * marks one entry as a non-selectable type. */
static void BuildRoster(UnitRoster *pRoster, Soldier *apUnits, Soldier *apType,
                        i32 nCount, Soldier *pSelected)
{
    memset(pRoster, 0, sizeof(*pRoster));
    for (i32 i = 0; i < nCount; ++i) {
        memset(&apUnits[i], 0, sizeof(apUnits[i]));
        apUnits[i].nUnitType = apType[i].nUnitType;
        pRoster->apUnits[i] = &apUnits[i];
    }
    pRoster->nCount = nCount;
    pRoster->pSelected = pSelected;
    pRoster->bActionLatch = 1;
}

static void TestFindSelected(void)
{
    UnitRoster roster;
    Soldier    aUnits[4];
    Soldier    aType[4];

    printf("Roster_FindSelected\n");
    memset(aType, 0, sizeof(aType));
    BuildRoster(&roster, aUnits, aType, 4, &aUnits[2]);
    CheckEqI32(Roster_FindSelected(&roster), 2, "  finds the selected slot");

    roster.pSelected = &aUnits[0];
    CheckEqI32(Roster_FindSelected(&roster), 0, "  finds slot 0");

    /* The original's miss state is nCount, not -1. */
    {
        Soldier outsider;

        memset(&outsider, 0, sizeof(outsider));
        roster.pSelected = &outsider;
        CheckEqI32(Roster_FindSelected(&roster), 4, "  miss ends at nCount");
    }
}

static void TestCycleNext(void)
{
    UnitRoster roster;
    Soldier    aUnits[4];
    Soldier    aType[4];

    printf("Roster_CycleNext -- advances to the next slot with wrap\n");
    memset(aType, 0, sizeof(aType));
    BuildRoster(&roster, aUnits, aType, 4, &aUnits[1]);
    LogReset();
    CheckEqI32(Roster_CycleNext(&roster, &g_hooks), 2, "  slot 1 -> 2");
    CheckEqI32(g_log.nSamples, 1, "  played one sample");
    CheckEqI32(g_log.nLastSample, 17, "  sample index 17");
    CheckEqI32(g_log.nSelects, 1, "  selected once");
    Check(g_log.pLastSelected == &aUnits[2], "  selected the advanced slot");
    CheckEqI32(g_log.nFocus, 1, "  focused");
    CheckEqI32(g_log.nRebuild, 0, "  CycleNext does NOT rebuild the visible list");

    /* Wrap at the end. */
    roster.pSelected = &aUnits[3];
    CheckEqI32(Roster_CycleNext(&roster, &g_hooks), 0, "  last slot wraps to 0");

    /* CycleNext clears gap_2[1] before selecting. */
    roster.pSelected = &aUnits[0];
    roster.bActionLatch = 1;
    (void)Roster_CycleNext(&roster, &g_hooks);
    CheckEqI32(roster.bActionLatch, 0, "  clears the large-cursor byte");

    /* No filter: CycleNext advances to a non-human slot too. */
    memset(aType, 0, sizeof(aType));
    aType[2].nUnitType = 9;
    BuildRoster(&roster, aUnits, aType, 4, &aUnits[1]);
    CheckEqI32(Roster_CycleNext(&roster, &g_hooks), 2,
               "  no type filter: lands on the alien slot");

    /* The documented miss deviation: no out-of-bounds read. */
    {
        Soldier outsider;

        memset(&outsider, 0, sizeof(outsider));
        BuildRoster(&roster, aUnits, aType, 4, &outsider);
        CheckEqI32(Roster_CycleNext(&roster, &g_hooks), -1,
                   "  missing selection refused instead of reading past the array");
    }
}

static void TestPrevNextFilter(void)
{
    UnitRoster roster;
    Soldier    aUnits[5];
    Soldier    aType[5];

    printf("Roster_SelectPrev / SelectNext -- skip nUnitType > 3\n");
    /* Slots: 0 human, 1 alien, 2 human, 3 alien, 4 human. */
    memset(aType, 0, sizeof(aType));
    aType[1].nUnitType = 9;
    aType[3].nUnitType = 12;
    BuildRoster(&roster, aUnits, aType, 5, &aUnits[0]);

    /* Forward from 0 skips the alien at 1 and lands on 2. */
    CheckEqI32(Roster_NextSelectableIndex(&roster), 2, "  next skips alien 1 -> 2");
    /* Forward from 2 lands on 4. */
    roster.pSelected = &aUnits[2];
    CheckEqI32(Roster_NextSelectableIndex(&roster), 4, "  next skips alien 3 -> 4");
    /* Forward from 4 wraps past 0 to 2 (0 is selectable, so it stops at 0). */
    roster.pSelected = &aUnits[4];
    CheckEqI32(Roster_NextSelectableIndex(&roster), 0, "  next wraps 4 -> 0");

    /* Backward from 0 wraps to the last selectable, 4. */
    roster.pSelected = &aUnits[0];
    CheckEqI32(Roster_PrevSelectableIndex(&roster), 4, "  prev wraps 0 -> 4");
    /* Backward from 4 skips alien 3 and lands on 2. */
    roster.pSelected = &aUnits[4];
    CheckEqI32(Roster_PrevSelectableIndex(&roster), 2, "  prev skips alien 3 -> 2");
    /* Backward from 2 skips alien 1 and lands on 0. */
    roster.pSelected = &aUnits[2];
    CheckEqI32(Roster_PrevSelectableIndex(&roster), 0, "  prev skips alien 1 -> 0");

    /* The wrappers play the sample, select, focus, then rebuild. */
    roster.pSelected = &aUnits[0];
    LogReset();
    CheckEqI32(Roster_SelectNext(&roster, &g_hooks), 2, "  SelectNext returns 2");
    CheckEqI32(g_log.nSamples, 1, "  played one sample");
    CheckEqI32(g_log.nSelects, 1, "  selected once");
    CheckEqI32(g_log.nFocus, 1, "  focused");
    CheckEqI32(g_log.nRebuild, 1, "  prev/next DO rebuild the visible list");
    Check(roster.pSelected == &aUnits[2], "  selection moved to slot 2");

    LogReset();
    CheckEqI32(Roster_SelectPrev(&roster, &g_hooks), 0, "  SelectPrev returns 0");
    CheckEqI32(g_log.nRebuild, 1, "  SelectPrev also rebuilds");

    /* A one-unit roster: next from 0 wraps to 0. */
    BuildRoster(&roster, aUnits, aType, 1, &aUnits[0]);
    CheckEqI32(Roster_NextSelectableIndex(&roster), 0, "  single unit next stays at 0");
    CheckEqI32(Roster_PrevSelectableIndex(&roster), 0, "  single unit prev stays at 0");
}

static void TestNoSelectableUnit(void)
{
    UnitRoster roster;
    Soldier    aUnits[3];
    Soldier    aType[3];

    printf("Roster_SelectNext -- no selectable unit terminates\n");
    memset(aType, 0, sizeof(aType));
    aType[0].nUnitType = 9;
    aType[1].nUnitType = 9;
    aType[2].nUnitType = 9;
    BuildRoster(&roster, aUnits, aType, 3, &aUnits[0]);
    /* The original's do-while would spin forever here; the port bails out. */
    CheckEqI32(Roster_NextSelectableIndex(&roster), -1,
               "  all-alien roster returns -1 instead of looping forever");
    CheckEqI32(Roster_PrevSelectableIndex(&roster), -1,
               "  and the same backwards");

    /* The wrapper must not select anything when no index was found. */
    LogReset();
    CheckEqI32(Roster_SelectNext(&roster, &g_hooks), -1, "  SelectNext refuses");
    CheckEqI32(g_log.nSelects, 0, "  nothing selected");
    CheckEqI32(g_log.nFocus, 0, "  nothing focused");
    CheckEqI32(g_log.nRebuild, 0, "  list not rebuilt");
}

static void TestEmptyRoster(void)
{
    UnitRoster roster;

    printf("Empty roster is inert\n");
    memset(&roster, 0, sizeof(roster));
    CheckEqI32(Roster_CycleNext(&roster, &g_hooks), -1, "  cycle on empty -> -1");
    CheckEqI32(Roster_SelectNext(&roster, &g_hooks), -1, "  next on empty -> -1");
    CheckEqI32(Roster_SelectPrev(&roster, &g_hooks), -1, "  prev on empty -> -1");
    CheckEqI32(Roster_CycleNext(NULL, &g_hooks), -1, "  NULL roster -> -1");
    CheckEqI32(Roster_CycleNext(&roster, NULL), -1, "  NULL hooks -> -1");
}

/* --- the filtered variants ------------------------------------------------- */

typedef struct FilterLog {
    bool bActive;
} FilterLog;

static bool H_IsFilterActive(void *pUser)
{
    return ((const FilterLog *)pUser)->bActive;
}

static RosterFilterHooks MakeFilterHooks(FilterLog *pLog)
{
    RosterFilterHooks fh;

    memset(&fh, 0, sizeof(fh));
    fh.base = g_hooks;
    fh.pfnIsFilterActive = H_IsFilterActive;
    fh.pFilterUser = pLog;
    return fh;
}

/* The extra filter term is the whole difference: a non-human is skipped only while
 * the ui-slot byte is set. With it clear, the first non-human is ACCEPTED. */
static void TestFilteredSelection(void)
{
    UnitRoster        roster;
    Soldier           aUnits[5];
    Soldier           aType[5];
    FilterLog         log;
    RosterFilterHooks fh = MakeFilterHooks(&log);

    printf("Roster_SelectNextFiltered -- the bFilterActive second term\n");
    /* Slots: 0 human, 1 alien, 2 human, 3 alien, 4 human. */
    memset(aType, 0, sizeof(aType));
    aType[1].nUnitType = 9;
    aType[3].nUnitType = 12;
    BuildRoster(&roster, aUnits, aType, 5, &aUnits[0]);

    /* Filter ACTIVE: non-humans are skipped, so 0 -> 2 (same as unfiltered). */
    log.bActive = true;
    CheckEqI32(Roster_SelectNextFiltered(&roster, &fh), 2,
               "  active filter skips the alien at 1");

    /* Filter INACTIVE: the alien at 1 is accepted immediately. */
    BuildRoster(&roster, aUnits, aType, 5, &aUnits[0]);
    log.bActive = false;
    CheckEqI32(Roster_SelectNextFiltered(&roster, &fh), 1,
               "  inactive filter ACCEPTS the very next alien");

    /* The prev variant uses the same two-term filter. */
    BuildRoster(&roster, aUnits, aType, 5, &aUnits[2]);
    log.bActive = true;
    CheckEqI32(Roster_SelectPrevFiltered(&roster, &fh), 0,
               "  prev with active filter skips the alien at 1");

    BuildRoster(&roster, aUnits, aType, 5, &aUnits[2]);
    log.bActive = false;
    CheckEqI32(Roster_SelectPrevFiltered(&roster, &fh), 1,
               "  prev with inactive filter accepts the alien at 1");
}

/* The NEXT variant selects and focuses every candidate it examines, so walking past
 * aliens is visible. The prev variant does not. */
static void TestNextFilteredActsPerCandidate(void)
{
    UnitRoster        roster;
    Soldier           aUnits[5];
    Soldier           aType[5];
    FilterLog         log;
    RosterFilterHooks fh = MakeFilterHooks(&log);

    printf("Roster_SelectNextFiltered -- selects once per examined candidate\n");
    /* 0 human, 1 alien, 2 alien, 3 human. Active filter walks 1 and 2. */
    memset(aType, 0, sizeof(aType));
    aType[1].nUnitType = 9;
    aType[2].nUnitType = 9;
    BuildRoster(&roster, aUnits, aType, 4, &aUnits[0]);
    log.bActive = true;

    LogReset();
    CheckEqI32(Roster_SelectNextFiltered(&roster, &fh), 3, "  lands on the human at 3");
    CheckEqI32(g_log.nSelects, 3,
               "  selected THREE times: once per candidate examined (1, 2, 3)");
    CheckEqI32(g_log.nFocus, 3, "  and focused three times");
    CheckEqI32(g_log.nRebuild, 1, "  but rebuilt the list only once, after the loop");

    /* The prev variant examines candidates without acting on them. */
    BuildRoster(&roster, aUnits, aType, 4, &aUnits[3]);
    log.bActive = true;
    LogReset();
    CheckEqI32(Roster_SelectPrevFiltered(&roster, &fh), 0, "  prev lands on 0");
    CheckEqI32(g_log.nSelects, 1, "  prev selects exactly once (it does not act per step)");
    CheckEqI32(g_log.nFocus, 1, "  and focuses once");
    CheckEqI32(g_log.nRebuild, 1, "  and rebuilds once");
}

static void TestFilteredTermination(void)
{
    UnitRoster        roster;
    Soldier           aUnits[3];
    Soldier           aType[3];
    FilterLog         log;
    RosterFilterHooks fh = MakeFilterHooks(&log);

    printf("Roster_SelectNextFiltered -- all-alien with the filter active terminates\n");
    memset(aType, 0, sizeof(aType));
    aType[0].nUnitType = 9;
    aType[1].nUnitType = 9;
    aType[2].nUnitType = 9;
    BuildRoster(&roster, aUnits, aType, 3, &aUnits[0]);
    log.bActive = true;
    /* Every candidate is rejected, so the original's do-while would spin; the port
     * bounds the walk and returns -1. */
    CheckEqI32(Roster_SelectNextFiltered(&roster, &fh), -1,
               "  no acceptable candidate -> -1 instead of spinning");
    CheckEqI32(Roster_SelectPrevFiltered(&roster, &fh), -1, "  and the same backwards");

    /* With the filter OFF, the very first alien is acceptable, so it terminates
     * with an answer instead. */
    log.bActive = false;
    CheckEqI32(Roster_SelectNextFiltered(&roster, &fh), 1,
               "  inactive filter accepts the next alien immediately");

    /* NULL safety. */
    CheckEqI32(Roster_SelectNextFiltered(NULL, &fh), -1, "  NULL roster -> -1");
    CheckEqI32(Roster_SelectNextFiltered(&roster, NULL), -1, "  NULL hooks -> -1");
}

/* --- side-list membership -------------------------------------------------- */

static void TestSideListsRemove(void)
{
    SideLists lists;
    Soldier   aUnits[7];
    Soldier  *apXcom[4];
    Soldier  *apEnemy[4];

    printf("SideLists_RemoveUnit -- XCom first, then enemy\n");
    memset(&lists, 0, sizeof(lists));
    memset(aUnits, 0, sizeof(aUnits));
    for (i32 i = 0; i < 4; ++i) {
        apXcom[i]  = &aUnits[i];
        apEnemy[i] = &aUnits[4 + i];
    }
    lists.apXcom  = apXcom;
    lists.nXcom   = 4;
    lists.apEnemy = apEnemy;
    lists.nEnemy  = 4;

    /* Remove from the middle of the XCom list: the tail compacts down. */
    Check(SideLists_RemoveUnit(&lists, &aUnits[1]), "  removed from XCom");
    CheckEqI32(lists.nXcom, 3, "  XCom count decremented");
    Check(lists.apXcom[0] == &aUnits[0], "  entry 0 unchanged");
    Check(lists.apXcom[1] == &aUnits[2], "  entry 1 was compacted up");
    Check(lists.apXcom[2] == &aUnits[3], "  entry 2 was compacted up");
    CheckEqI32(lists.nEnemy, 4, "  enemy list untouched");

    /* Removing the LAST entry only drops the count -- nothing to compact. */
    Check(SideLists_RemoveUnit(&lists, &aUnits[3]), "  removed the last XCom entry");
    CheckEqI32(lists.nXcom, 2, "  count decremented");
    Check(lists.apXcom[0] == &aUnits[0], "  entry 0 still there");
    Check(lists.apXcom[1] == &aUnits[2], "  entry 1 still there");

    /* An enemy unit is found only after the XCom search misses. */
    Check(SideLists_RemoveUnit(&lists, &aUnits[5]), "  removed from the enemy list");
    CheckEqI32(lists.nEnemy, 3, "  enemy count decremented");
    Check(lists.apEnemy[1] == &aUnits[6], "  enemy entry compacted up");
    CheckEqI32(lists.nXcom, 2, "  XCom count unchanged");

    /* A unit in neither list changes nothing. */
    {
        Soldier outsider;

        memset(&outsider, 0, sizeof(outsider));
        Check(!SideLists_RemoveUnit(&lists, &outsider), "  unknown unit not removed");
        CheckEqI32(lists.nXcom, 2, "  XCom count unchanged");
        CheckEqI32(lists.nEnemy, 3, "  enemy count unchanged");
    }

    Check(!SideLists_RemoveUnit(&lists, NULL), "  NULL unit refused");
    Check(!SideLists_RemoveUnit(NULL, &aUnits[0]), "  NULL lists refused");
}

/* A unit present in both lists must be removed from the XCom list only, because the
 * original returns as soon as the XCom search hits. */
static void TestSideListsRemovesXcomFirst(void)
{
    SideLists lists;
    Soldier   aUnits[4];
    Soldier  *apXcom[2];
    Soldier  *apEnemy[2];

    printf("SideLists_RemoveUnit -- a unit in BOTH lists leaves the enemy list alone\n");
    memset(&lists, 0, sizeof(lists));
    memset(aUnits, 0, sizeof(aUnits));
    apXcom[0]  = &aUnits[0];
    apXcom[1]  = &aUnits[1];
    apEnemy[0] = &aUnits[0];  /* the same unit in both */
    apEnemy[1] = &aUnits[2];
    lists.apXcom  = apXcom;
    lists.nXcom   = 2;
    lists.apEnemy = apEnemy;
    lists.nEnemy  = 2;

    Check(SideLists_RemoveUnit(&lists, &aUnits[0]), "  removed");
    CheckEqI32(lists.nXcom, 1, "  XCom count dropped");
    CheckEqI32(lists.nEnemy, 2, "  enemy count is UNCHANGED");
    Check(lists.apEnemy[0] == &aUnits[0], "  the enemy entry still points at it");
}

/* --- soldier-list pass ----------------------------------------------------- */

static void TestMarkAllSoldiersSeen(void)
{
    Soldier aSoldiers[4];
    Soldier *apSoldiers[4];
    u8      aLinkStorage[4][300];

    printf("SideLists_MarkAllSoldiersSeen\n");
    memset(aSoldiers, 0, sizeof(aSoldiers));
    memset(aLinkStorage, 0, sizeof(aLinkStorage));
    for (i32 i = 0; i < 4; ++i) {
        apSoldiers[i] = &aSoldiers[i];
    }

    /* 0: live link + HP -> marked. 1: link but zero HP -> skipped.
     * 2: no link -> skipped. 3: live link + HP -> marked. */
    aSoldiers[0].pLinked1040 = aLinkStorage[0];
    aSoldiers[0].uHp = 10;
    aSoldiers[1].pLinked1040 = aLinkStorage[1];
    aSoldiers[1].uHp = 0;
    aSoldiers[2].pLinked1040 = NULL;
    aSoldiers[2].uHp = 10;
    aSoldiers[3].pLinked1040 = aLinkStorage[3];
    aSoldiers[3].uHp = 5;

    CheckEqI32(SideLists_MarkAllSoldiersSeen(apSoldiers, 4), 2, "  two soldiers marked");
    CheckEqI32(*(u16 *)(aLinkStorage[0] + 250), 1, "  live link counter incremented");
    CheckEqI32(*(u16 *)(aLinkStorage[3] + 250), 1, "  second live link incremented");
    CheckEqI32(*(u16 *)(aLinkStorage[1] + 250), 0, "  a dead soldier's link is NOT counted");
    Check(aSoldiers[0].pLinked1040 == NULL, "  the link was cleared");
    Check(aSoldiers[1].pLinked1040 != NULL, "  a dead soldier keeps its link");
    Check(aSoldiers[3].pLinked1040 == NULL, "  the second link was cleared");

    /* Running again is a no-op: every cleared link stays cleared, and the dead
     * soldier's link is still skipped. */
    CheckEqI32(SideLists_MarkAllSoldiersSeen(apSoldiers, 4), 0, "  second pass marks none");
    CheckEqI32(*(u16 *)(aLinkStorage[0] + 250), 1, "  counter not incremented twice");

    /* A NULL slot is tolerated. */
    apSoldiers[0] = NULL;
    CheckEqI32(SideLists_MarkAllSoldiersSeen(apSoldiers, 4), 0, "  NULL slot tolerated");

    CheckEqI32(SideLists_MarkAllSoldiersSeen(NULL, 4), 0, "  NULL list -> 0");
    CheckEqI32(SideLists_MarkAllSoldiersSeen(apSoldiers, 0), 0, "  zero count -> 0");
}

/* --- soldier searches ------------------------------------------------------ */

/* The three conditions are ANDed: rank match, alive (uHp != 0), and NOT large.
 * Each is exercised in isolation, because a rank match alone must not count. */
static void TestSoldierSearches(void)
{
    Soldier  aUnits[5];
    Soldier *apUnits[5];

    printf("Soldier_FindNextSmallByType / Soldier_CountSmallByType\n");
    memset(aUnits, 0, sizeof(aUnits));
    for (i32 i = 0; i < 5; ++i) {
        apUnits[i] = &aUnits[i];
        aUnits[i].uHp = 10;
    }

    /* Rank 2 at slots 1 and 3; slot 3 is a LARGE unit type, so it must be skipped.
     * Slot 4 has the right rank but is dead. */
    aUnits[1].bRankType = 2;
    aUnits[3].bRankType = 2;
    aUnits[4].bRankType = 2;
    aUnits[4].uHp = 0;
    aUnits[3].nUnitType = 5;   /* a large type (g_aUnitTypeIsLarge[5] == 1) */
    aUnits[0].bRankType = 1;
    aUnits[2].bRankType = 1;

    CheckEqI32(Soldier_FindNextSmallByType(apUnits, 5, 2, 0), 1,
               "  finds the first small live unit of rank 2");
    /* Starting at the large one: it is skipped, and the dead one after it too. */
    CheckEqI32(Soldier_FindNextSmallByType(apUnits, 5, 2, 3), -1,
               "  skips the large unit and the dead one");
    CheckEqI32(Soldier_FindNextSmallByType(apUnits, 5, 2, 1), 1,
               "  nStart on a match returns it");
    CheckEqI32(Soldier_FindNextSmallByType(apUnits, 5, 9, 0), -1,
               "  an absent rank returns -1");

    /* nStart at or past the count fails immediately. */
    CheckEqI32(Soldier_FindNextSmallByType(apUnits, 5, 2, 5), -1,
               "  nStart == count -> -1");
    CheckEqI32(Soldier_FindNextSmallByType(apUnits, 5, 2, 99), -1,
               "  nStart past the count -> -1");

    CheckEqI32(Soldier_CountSmallByType(apUnits, 5, 2), 1,
               "  only one rank-2 small live unit counts");
    CheckEqI32(Soldier_CountSmallByType(apUnits, 5, 1), 2, "  two rank-1 units");
    CheckEqI32(Soldier_CountSmallByType(apUnits, 5, 9), 0, "  no rank-9 units");

    /* A large unit alone must not count even with the right rank and full HP. */
    for (i32 i = 0; i < 5; ++i) {
        aUnits[i].nUnitType = 5;
        aUnits[i].bRankType = 3;
        aUnits[i].uHp = 10;
    }
    CheckEqI32(Soldier_CountSmallByType(apUnits, 5, 3), 0,
               "  all-large roster counts zero");
    CheckEqI32(Soldier_FindNextSmallByType(apUnits, 5, 3, 0), -1,
               "  and finds none");

    /* An all-dead roster counts zero. */
    for (i32 i = 0; i < 5; ++i) {
        aUnits[i].nUnitType = 0;
        aUnits[i].uHp = 0;
    }
    CheckEqI32(Soldier_CountSmallByType(apUnits, 5, 3), 0, "  all-dead counts zero");

    /* NULL slots are skipped rather than dereferenced. */
    memset(aUnits, 0, sizeof(aUnits));
    for (i32 i = 0; i < 5; ++i) {
        aUnits[i].uHp = 10;
    }
    apUnits[1] = NULL;
    apUnits[2] = NULL;
    CheckEqI32(Soldier_CountSmallByType(apUnits, 5, 0), 3, "  NULL slots skipped");

    CheckEqI32(Soldier_FindNextSmallByType(NULL, 5, 0, 0), -1, "  NULL list -> -1");
    CheckEqI32(Soldier_CountSmallByType(NULL, 5, 0), 0, "  NULL list -> 0");
    CheckEqI32(Soldier_CountSmallByType(apUnits, 0, 0), 0, "  zero count -> 0");
}

static void TestPickBestScore(void)
{
    Soldier  aUnits[4];
    Soldier *apUnits[4];
    i32      anIdx[8] = { 0, 1, 2, 3, -1, -1, -1, -1 };
    i32      nBest = -1;
    i32      nBestIdx = -1;

    printf("Soldier_PickBestScoreAmong\n");
    memset(aUnits, 0, sizeof(aUnits));
    for (i32 i = 0; i < 4; ++i) {
        apUnits[i] = &aUnits[i];
    }
    aUnits[0].uKills = 1;  aUnits[0].uMissions = 0;  /* 5  */
    aUnits[1].uKills = 0;  aUnits[1].uMissions = 3;  /* 30 */
    aUnits[2].uKills = 2;  aUnits[2].uMissions = 1;  /* 20 */
    aUnits[3].uKills = 0;  aUnits[3].uMissions = 0;  /* 0  */

    CheckEqI32(Soldier_PickBestScoreAmong(apUnits, anIdx, &nBest, &nBestIdx), 30,
               "best score is 5*(kills + 2*missions)");
    CheckEqI32(nBestIdx, 1, "winning slot index reported");

    /* A single nonzero slot wins. */
    anIdx[1] = -1;
    anIdx[2] = -1;
    anIdx[3] = -1;
    CheckEqI32(Soldier_PickBestScoreAmong(apUnits, anIdx, &nBest, &nBestIdx), 5,
               "only slot 0 remains");
    CheckEqI32(nBestIdx, 0, "slot 0 wins");

    /* All empty -> 0, and the port's documented deviation (0, not garbage). */
    anIdx[0] = -1;
    CheckEqI32(Soldier_PickBestScoreAmong(apUnits, anIdx, &nBest, &nBestIdx), 0,
               "all-empty returns 0");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestFindSelected();
    TestCycleNext();
    TestPrevNextFilter();
    TestNoSelectableUnit();
    TestEmptyRoster();
    TestFilteredSelection();
    TestNextFilteredActsPerCandidate();
    TestFilteredTermination();
    TestSideListsRemove();
    TestSideListsRemovesXcomFirst();
    TestMarkAllSoldiersSeen();
    TestSoldierSearches();
    TestPickBestScore();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
