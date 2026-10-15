/* test_roster.c - checks the XCom roster navigation policies.
 *
 * The three policies are transcribed from the decompilation; the tests pin the
 * exact wrap arithmetic and the nUnitType > 3 filter that distinguishes them.
 */
#include "game/pufo_roster.h"

#include "game/pufo_mapcell.h"
#include "game/pufo_tactui.h"

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

/* The rebuild is no longer a hook: prev/next call Inv_BuildVisibleList on this
 * state. Each roster unit gets a cell holding a DISTINCT number of ground items
 * (unit i holds i + 1), so the resulting nInvListCount identifies which unit was
 * rebuilt and whether a rebuild happened at all. A sentinel value of 9 marks "no
 * rebuild happened". */
static TactUiState g_Ui;
static ObjectRec   g_aObj[25];
static TactItem    g_aItem[25];
static MapCell     g_aCell[5];

static void ListFixtureReset(void)
{
    i32 i;

    memset(g_aObj, 0, sizeof(g_aObj));
    memset(g_aItem, 0, sizeof(g_aItem));
    memset(g_aCell, 0, sizeof(g_aCell));
    for (i = 0; i < 25; ++i) {
        g_aObj[i].uSortKey = (u16)i;
        g_aItem[i].pObj = &g_aObj[i];
    }
}

/* Arm the sentinel: a rebuild always rewrites the count, so 9 survives only when
 * no rebuild ran. The filter is "all", so the count equals the item count. */
static void ListSentinel(void)
{
    memset(&g_Ui, 0, sizeof(g_Ui));
    g_Ui.nInvListSel = 0xFF;
    g_Ui.nInvListCount = 9;
}

static const RosterHooks g_hooks = {
    OnPlaySample, OnSelectUnit, OnFocusSelected, &g_Ui, NULL
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
    ListFixtureReset();
    for (i32 i = 0; i < nCount; ++i) {
        i32 j;

        memset(&apUnits[i], 0, sizeof(apUnits[i]));
        apUnits[i].nUnitType = apType[i].nUnitType;
        /* Unit i owns a cell with i + 1 distinct ground items. */
        if (i < 5) {
            for (j = 0; j <= i; ++j) {
                g_aItem[i * 5 + j].pNext =
                    (j + 1 <= i) ? &g_aItem[i * 5 + j + 1] : NULL;
            }
            g_aCell[i].pItemList = &g_aItem[i * 5];
            apUnits[i].pCell = &g_aCell[i];
        }
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
    ListSentinel();
    CheckEqI32(Roster_CycleNext(&roster, &g_hooks), 2, "  slot 1 -> 2");
    CheckEqI32(g_log.nSamples, 1, "  played one sample");
    CheckEqI32(g_log.nLastSample, 17, "  sample index 17");
    CheckEqI32(g_log.nSelects, 1, "  selected once");
    Check(g_log.pLastSelected == &aUnits[2], "  selected the advanced slot");
    CheckEqI32(g_log.nFocus, 1, "  focused");
    CheckEqI32(g_Ui.nInvListCount, 9, "  CycleNext does NOT rebuild the visible list");

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
    ListSentinel();
    CheckEqI32(Roster_SelectNext(&roster, &g_hooks), 2, "  SelectNext returns 2");
    CheckEqI32(g_log.nSamples, 1, "  played one sample");
    CheckEqI32(g_log.nSelects, 1, "  selected once");
    CheckEqI32(g_log.nFocus, 1, "  focused");
    /* Slot 2 owns three ground items, so a rebuild lands on 3, not the sentinel. */
    CheckEqI32(g_Ui.nInvListCount, 3, "  prev/next DO rebuild the visible list");
    Check(roster.pSelected == &aUnits[2], "  selection moved to slot 2");

    LogReset();
    ListSentinel();
    CheckEqI32(Roster_SelectPrev(&roster, &g_hooks), 0, "  SelectPrev returns 0");
    /* Slot 0 owns one item. */
    CheckEqI32(g_Ui.nInvListCount, 1, "  SelectPrev also rebuilds");

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
    ListSentinel();
    CheckEqI32(Roster_SelectNext(&roster, &g_hooks), -1, "  SelectNext refuses");
    CheckEqI32(g_log.nSelects, 0, "  nothing selected");
    CheckEqI32(g_log.nFocus, 0, "  nothing focused");
    CheckEqI32(g_Ui.nInvListCount, 9, "  list not rebuilt");
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
    ListSentinel();
    CheckEqI32(Roster_SelectNextFiltered(&roster, &fh), 3, "  lands on the human at 3");
    CheckEqI32(g_log.nSelects, 3,
               "  selected THREE times: once per candidate examined (1, 2, 3)");
    CheckEqI32(g_log.nFocus, 3, "  and focused three times");
    /* The list is the FINAL unit's (slot 3 owns four items), so the rebuild ran
     * once, after the loop, on the unit that was actually selected. */
    CheckEqI32(g_Ui.nInvListCount, 4,
               "  rebuilt once after the loop, on the final unit");

    /* The prev variant examines candidates without acting on them. */
    BuildRoster(&roster, aUnits, aType, 4, &aUnits[3]);
    log.bActive = true;
    LogReset();
    ListSentinel();
    CheckEqI32(Roster_SelectPrevFiltered(&roster, &fh), 0, "  prev lands on 0");
    CheckEqI32(g_log.nSelects, 1, "  prev selects exactly once (it does not act per step)");
    CheckEqI32(g_log.nFocus, 1, "  and focuses once");
    CheckEqI32(g_Ui.nInvListCount, 1, "  and rebuilds once");
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

/* --- the post-mission rank ladder ------------------------------------------ */

/* A live, small, operative soldier of the given rank with a chosen mission score.
 * Score is 5 * (uKills + 2 * uMissions), so kills alone sets it exactly. */
static Soldier *PromoteMake(Soldier *pUnit, u8 nRank, i32 nKills)
{
    memset(pUnit, 0, sizeof(*pUnit));
    pUnit->nUnitType = 0;   /* a small, non-tank type */
    pUnit->uHp       = 50;
    pUnit->bStatus   = 0;   /* UNIT_STATUS_OPERATIVE */
    pUnit->bRankType = nRank;
    pUnit->uKills    = (u16)nKills;
    return pUnit;
}

static void TestPromoteRookieRung(void)
{
    Soldier  aUnits[4];
    Soldier *apUnits[4];
    SoldierPromoteEnv env;
    u16 nCount = 4;

    printf("Soldier_PromoteByTypeScores - the bottom rung (0 -> 1)\n");
    for (i32 i = 0; i < 4; ++i) {
        apUnits[i] = &aUnits[i];
    }
    env.apSoldiers = apUnits;
    env.pnSoldierCount = &nCount;

    /* Score 50 clears the gate, 45 does not (the ARM test is `> 45`, BLE skips), and a dead
     * soldier with a huge score does not. */
    PromoteMake(&aUnits[0], 0, 10);   /* 50  */
    PromoteMake(&aUnits[1], 0, 9);    /* 45  */
    PromoteMake(&aUnits[2], 0, 20);   /* 100, but dead */
    aUnits[2].uHp = 0;
    PromoteMake(&aUnits[3], 1, 0);    /* one rank-1 unit: count(1)/3 == 0, so rung 1->2 is off */

    Soldier_PromoteByTypeScores(&env);

    CheckEqI32(aUnits[0].bRankType, 1, "  score 50 promotes");
    CheckEqI32(aUnits[0].bRankDirty, 1, "  and marks the promotion");
    CheckEqI32(aUnits[1].bRankType, 0, "  score exactly 45 does not");
    CheckEqI32(aUnits[2].bRankType, 0, "  a dead soldier is not promoted");
    CheckEqI32(aUnits[3].bRankType, 1, "  the rank-1 unit is untouched by this rung");
}

static void TestPromoteNavigatorRung(void)
{
    Soldier  aUnits[4];
    Soldier *apUnits[4];
    SoldierPromoteEnv env;
    u16 nCount = 4;

    printf("Soldier_PromoteByTypeScores - 2 -> 3, threshold 125\n");
    for (i32 i = 0; i < 4; ++i) {
        apUnits[i] = &aUnits[i];
    }
    env.apSoldiers = apUnits;
    env.pnSoldierCount = &nCount;

    /* The better of the two rank-2 soldiers is promoted; the other stays. */
    PromoteMake(&aUnits[0], 2, 40);   /* 200 */
    PromoteMake(&aUnits[1], 2, 26);   /* 130 */
    PromoteMake(&aUnits[2], 0, 0);
    PromoteMake(&aUnits[3], 0, 0);
    Soldier_PromoteByTypeScores(&env);
    CheckEqI32(aUnits[0].bRankType, 3, "  the higher score is promoted");
    CheckEqI32(aUnits[1].bRankType, 2, "  the lower one is left alone");

    /* Candidate B wins when its score is the better one. */
    PromoteMake(&aUnits[0], 2, 10);   /* 50  */
    PromoteMake(&aUnits[1], 2, 40);   /* 200 */
    Soldier_PromoteByTypeScores(&env);
    CheckEqI32(aUnits[0].bRankType, 2, "  candidate A stays");
    CheckEqI32(aUnits[1].bRankType, 3, "  candidate B is promoted");

    /* With the best score short of 125 the whole rung is skipped, candidate B included. */
    PromoteMake(&aUnits[0], 2, 24);   /* 120 */
    PromoteMake(&aUnits[1], 2, 20);   /* 100 */
    Soldier_PromoteByTypeScores(&env);
    CheckEqI32(aUnits[0].bRankType, 2, "  120 is short of 125");
    CheckEqI32(aUnits[1].bRankType, 2, "  and the second candidate is not tried either");

    /* The rung needs count(3) < 2: two rank-3 soldiers already on the roster close it. */
    PromoteMake(&aUnits[0], 2, 60);   /* 300 */
    PromoteMake(&aUnits[1], 2, 50);   /* 250 */
    PromoteMake(&aUnits[2], 3, 0);
    PromoteMake(&aUnits[3], 3, 0);
    Soldier_PromoteByTypeScores(&env);
    CheckEqI32(aUnits[0].bRankType, 2, "  two rank-3 soldiers close the rung");

    /* And it needs count(2) >= 2: one rank-2 soldier opens nothing. */
    PromoteMake(&aUnits[0], 2, 60);
    PromoteMake(&aUnits[1], 0, 0);
    PromoteMake(&aUnits[2], 0, 0);
    PromoteMake(&aUnits[3], 0, 0);
    Soldier_PromoteByTypeScores(&env);
    CheckEqI32(aUnits[0].bRankType, 2, "  one rank-2 soldier is not enough");
}

static void TestPromoteEngineerQuirk(void)
{
    Soldier  aUnits[3];
    Soldier *apUnits[3];
    SoldierPromoteEnv env;
    u16 nCount = 3;

    printf("Soldier_PromoteByTypeScores - the engineer rung tests == 0, the leader rung < 2\n");
    for (i32 i = 0; i < 3; ++i) {
        apUnits[i] = &aUnits[i];
    }
    env.apSoldiers = apUnits;
    env.pnSoldierCount = &nCount;

    /* Leader rung: one rank-4 soldier is still under the < 2 gate, so the best rank-3 climbs. */
    PromoteMake(&aUnits[0], 4, 0);
    PromoteMake(&aUnits[1], 3, 60);   /* 300 */
    PromoteMake(&aUnits[2], 3, 56);   /* 280 */
    Soldier_PromoteByTypeScores(&env);
    CheckEqI32(aUnits[1].bRankType, 4, "  one rank-4 soldier does not close the leader rung");

    /* Engineer rung: one rank-5 soldier DOES close it, because the gate is == 0. */
    PromoteMake(&aUnits[0], 5, 0);
    PromoteMake(&aUnits[1], 4, 80);   /* 400 */
    PromoteMake(&aUnits[2], 4, 76);   /* 380 */
    Soldier_PromoteByTypeScores(&env);
    CheckEqI32(aUnits[1].bRankType, 4, "  one rank-5 soldier closes the engineer rung");
    CheckEqI32(aUnits[2].bRankType, 4, "  so no second engineer is ever made");

    /* With no engineer at all the best rank-4 clears 375 and becomes one. */
    PromoteMake(&aUnits[0], 0, 0);
    PromoteMake(&aUnits[1], 4, 80);   /* 400 */
    PromoteMake(&aUnits[2], 4, 76);   /* 380 */
    Soldier_PromoteByTypeScores(&env);
    CheckEqI32(aUnits[1].bRankType, 5, "  an empty engineer slot lets the best rank-4 in");
    CheckEqI32(aUnits[2].bRankType, 4, "  only one of them");
}

static void TestPromoteSoldierRung(void)
{
    Soldier  aUnits[7];
    Soldier *apUnits[7];
    SoldierPromoteEnv env;
    u16 nCount = 7;
    i32 nAt2;

    printf("Soldier_PromoteByTypeScores - 1 -> 2, count(1)/3 attempts, threshold 90\n");
    for (i32 i = 0; i < 7; ++i) {
        apUnits[i] = &aUnits[i];
    }
    env.apSoldiers = apUnits;
    env.pnSoldierCount = &nCount;

    /* Three rank-1 soldiers -> one attempt, and only the best clears 90. */
    for (i32 i = 0; i < 7; ++i) {
        PromoteMake(&aUnits[i], 0, 0);
    }
    PromoteMake(&aUnits[0], 1, 20);   /* 100 */
    PromoteMake(&aUnits[1], 1, 19);   /* 95  */
    PromoteMake(&aUnits[2], 1, 18);   /* 90  */
    Soldier_PromoteByTypeScores(&env);
    nAt2 = 0;
    for (i32 i = 0; i < 7; ++i) {
        if (aUnits[i].bRankType == 2) {
            nAt2++;
        }
    }
    CheckEqI32(nAt2, 1, "  three rank-1 soldiers buy exactly one promotion");
    CheckEqI32(aUnits[0].bRankType, 2, "  it goes to the best score");
    CheckEqI32(aUnits[1].bRankType, 1, "  the runner-up waits");

    /* Six rank-1 soldiers -> two attempts, taken best first. */
    for (i32 i = 0; i < 7; ++i) {
        PromoteMake(&aUnits[i], 0, 0);
    }
    for (i32 i = 0; i < 6; ++i) {
        PromoteMake(&aUnits[i], 1, 20 - i);   /* 100, 95, 90, 85, 80, 75 */
    }
    Soldier_PromoteByTypeScores(&env);
    CheckEqI32(aUnits[0].bRankType, 2, "  first attempt takes the best");
    CheckEqI32(aUnits[1].bRankType, 2, "  second attempt takes the next best");
    CheckEqI32(aUnits[2].bRankType, 1, "  third is left at rank 1");

    /* The block is skipped outright when the roster already holds at least attempts rank-2
     * soldiers: attempts 1, one rank-2 already there. */
    for (i32 i = 0; i < 7; ++i) {
        PromoteMake(&aUnits[i], 0, 0);
    }
    PromoteMake(&aUnits[0], 2, 0);
    PromoteMake(&aUnits[1], 1, 40);   /* 200 */
    PromoteMake(&aUnits[2], 1, 39);   /* 195 */
    PromoteMake(&aUnits[3], 1, 38);   /* 190 */
    Soldier_PromoteByTypeScores(&env);
    CheckEqI32(aUnits[1].bRankType, 1, "  attempts 1 against one existing rank-2 -> no promotion");

    /* Three rank-1 soldiers all short of 90: the attempt runs and promotes nobody. */
    for (i32 i = 0; i < 7; ++i) {
        PromoteMake(&aUnits[i], 0, 0);
    }
    PromoteMake(&aUnits[0], 1, 17);   /* 85 */
    PromoteMake(&aUnits[1], 1, 16);   /* 80 */
    PromoteMake(&aUnits[2], 1, 15);   /* 75 */
    Soldier_PromoteByTypeScores(&env);
    CheckEqI32(aUnits[0].bRankType, 1, "  85 is short of 90");
    CheckEqI32(aUnits[1].bRankType, 1, "  nobody is promoted");

    /* The 1 -> 2 scans test bRankType alone: a DEAD rank-1 soldier with the best score is the
     * one that climbs, even though count(1) only counted the live ones. */
    for (i32 i = 0; i < 7; ++i) {
        PromoteMake(&aUnits[i], 0, 0);
    }
    for (i32 i = 0; i < 3; ++i) {
        PromoteMake(&aUnits[i], 1, 10);   /* 50 each, live */
    }
    PromoteMake(&aUnits[4], 1, 40);       /* 200, dead */
    aUnits[4].uHp = 0;
    Soldier_PromoteByTypeScores(&env);
    CheckEqI32(aUnits[4].bRankType, 2, "  the dead high-scorer is promoted -- no alive test here");
    CheckEqI32(aUnits[0].bRankType, 1, "  the live ones are not");
}

static void TestPromoteRungOrder(void)
{
    Soldier  aUnits[4];
    Soldier *apUnits[4];
    SoldierPromoteEnv env;
    u16 nCount = 4;

    printf("Soldier_PromoteByTypeScores - rungs run top-down, one step per call\n");
    for (i32 i = 0; i < 4; ++i) {
        apUnits[i] = &aUnits[i];
    }
    env.apSoldiers = apUnits;
    env.pnSoldierCount = &nCount;

    for (i32 i = 0; i < 4; ++i) {
        PromoteMake(&aUnits[i], 0, 0);
    }
    PromoteMake(&aUnits[0], 3, 60);   /* 300 */
    PromoteMake(&aUnits[1], 3, 56);   /* 280 */
    PromoteMake(&aUnits[2], 2, 40);   /* 200 */
    PromoteMake(&aUnits[3], 2, 30);   /* 150 */
    Soldier_PromoteByTypeScores(&env);

    CheckEqI32(aUnits[0].bRankType, 4, "  the best rank-3 takes the 3 -> 4 rung");
    CheckEqI32(aUnits[2].bRankType, 3, "  the best rank-2 takes the 2 -> 3 rung");
    CheckEqI32(aUnits[2].bRankType != 4, 1,
               "  and it does not then climb again -- the 3 -> 4 rung already ran");
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
    TestPromoteRookieRung();
    TestPromoteNavigatorRung();
    TestPromoteEngineerQuirk();
    TestPromoteSoldierRung();
    TestPromoteRungOrder();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
