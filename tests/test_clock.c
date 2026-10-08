/* test_clock.c - GameTime_Decode @ 0x5cfd4, GameTime_Reset @ 0x5c56c and
 * Geoscape_AdvanceTime @ 0x5ce40. The month table {31,28,...} was read out of
 * the IDB at dword_AB270; the tick gates (minute paused, hour/day not) and the
 * month-change trigger are transcribed from the decompilation.
 */
#include "game/pufo_clock.h"

#include <stdio.h>

static int g_nFailures;

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot == nWant) {
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

static void TestDecode(void)
{
    printf("GameTime_Decode\n");
    GameTime t = {0};

    t.nTotalSec = 0;
    GameTime_Decode(&t);
    CheckEqI32(t.nYear, 1999, "year at 0");
    CheckEqI32(t.nMonth, 1, "month at 0");
    CheckEqI32(t.nDay, 1, "day at 0");
    CheckEqI32(t.nWeekday, 5, "weekday at 0 ((0+4)%7+1)");
    CheckEqI32(t.nHour, 0, "hour at 0");
    CheckEqI32(t.nSecond, 0, "returns second remainder");

    t.nTotalSec = 86400 - 1;
    GameTime_Decode(&t);
    CheckEqI32(t.nDay, 1, "last second of day 1 is still day 1");
    CheckEqI32(t.nHour, 23, "23:59:59 hour");
    CheckEqI32(t.nMinute, 59, "23:59:59 minute");
    CheckEqI32(t.nSecond, 59, "23:59:59 second");

    t.nTotalSec = 86400;
    GameTime_Decode(&t);
    CheckEqI32(t.nDay, 2, "day rolls at 86400");
    CheckEqI32(t.nWeekday, 6, "weekday advances with the day");

    /* 31 days of January pass; the month walk stops when the running total
     * strictly exceeds the remainder, so 31*86400 is February 1. */
    t.nTotalSec = 31 * 86400;
    GameTime_Decode(&t);
    CheckEqI32(t.nMonth, 2, "month 2 at 31 days");
    CheckEqI32(t.nDay, 1, "day 1 of month 2");

    /* February is 28 days in the fixed table. */
    t.nTotalSec = (31 + 28) * 86400;
    GameTime_Decode(&t);
    CheckEqI32(t.nMonth, 3, "month 3 at 59 days");

    t.nTotalSec = 31536000;
    GameTime_Decode(&t);
    CheckEqI32(t.nYear, 2000, "year 2000 at 31536000");
    CheckEqI32(t.nMonth, 1, "month 1 at year roll");
    CheckEqI32(t.nDay, 1, "day 1 at year roll");
}

static void TestReset(void)
{
    printf("GameTime_Reset\n");
    GameTime t = {0};
    t.nTotalSec = 999999;
    t.bPaused = 1;
    t.nUnk34 = 7;
    GameTime_Reset(&t);
    CheckEqI32(t.nTotalSec, 0, "total cleared");
    CheckEqI32(t.bPaused, 0, "pause cleared");
    CheckEqI32(t.nUnk34, 0, "nUnk34 cleared");
    CheckEqI32(t.nLastTickMinSec, 0, "minute stamp cleared");
    CheckEqI32(t.nLastTickHourSec, 0, "hour stamp cleared");
    CheckEqI32(t.nLastTickDaySec, 0, "day stamp cleared");
    CheckEqI32(t.nMonthEpochSec, 0, "month epoch cleared");
    CheckEqI32(t.nYear, 1999, "decoded after reset");
}

/* --- tick recording --- */

static int g_nShips, g_nHourly, g_nBases, g_nMonthly;

static void Hook_Ships(void *pUser)  { (void)pUser; ++g_nShips; }
static void Hook_Hourly(void *pUser) { (void)pUser; ++g_nHourly; }
static void Hook_Bases(void *pUser)  { (void)pUser; ++g_nBases; }
static void Hook_Monthly(void *pUser){ (void)pUser; ++g_nMonthly; }

static void TestAdvance(void)
{
    printf("Geoscape_AdvanceTime\n");
    GeoscapeClockHooks hooks = { Hook_Ships, Hook_Hourly, Hook_Bases,
                                 Hook_Monthly, 0 };
    GameTime t;
    GameTime_Reset(&t);
    g_nShips = g_nHourly = g_nBases = g_nMonthly = 0;

    Geoscape_AdvanceTime(&t, 59, &hooks);
    CheckEqI32(g_nShips, 0, "59 s is under one minute tick");

    Geoscape_AdvanceTime(&t, 1, &hooks);
    CheckEqI32(g_nShips, 1, "60th second fires one ship tick");
    CheckEqI32(g_nHourly, 0, "no hour tick yet");

    Geoscape_AdvanceTime(&t, 3600 - 60, &hooks);
    CheckEqI32(g_nShips, 60, "whole hour fires the remaining 59 minute ticks");
    CheckEqI32(g_nHourly, 1, "one hour tick");
    CheckEqI32(g_nBases, 0, "no day tick yet");

    Geoscape_AdvanceTime(&t, 86400 - 3600, &hooks);
    CheckEqI32(g_nHourly, 24, "one day is 24 hours total");
    CheckEqI32(g_nBases, 1, "one day tick");

    /* Paused: minute ticks are skipped, hour and day ticks are not. */
    t.bPaused = 1;
    int shipsBefore = g_nShips, hourlyBefore = g_nHourly;
    Geoscape_AdvanceTime(&t, 3600, &hooks);
    CheckEqI32(g_nShips, shipsBefore, "paused skips ship ticks");
    CheckEqI32(g_nHourly, hourlyBefore + 1, "paused still ticks hourly");
    CheckEqI32(t.nLastTickMinSec, t.nTotalSec, "minute stamp advances while paused");
    t.bPaused = 0;

    /* Negative delta: ARM division truncates toward zero, nothing fires,
     * but the stamps are re-based to the new total. */
    shipsBefore = g_nShips;
    Geoscape_AdvanceTime(&t, -7200, &hooks);
    CheckEqI32(g_nShips, shipsBefore, "negative delta fires nothing");
    CheckEqI32(t.nLastTickMinSec, t.nTotalSec, "stamps re-based after negative delta");

    /* Month change: jump past 31 days from the current month boundary and
     * the monthly funding hook fires exactly once. */
    GameTime_Reset(&t);
    g_nMonthly = 0;
    Geoscape_AdvanceTime(&t, 31 * 86400, &hooks);
    CheckEqI32(t.nMonth, 2, "month advanced");
    CheckEqI32(g_nMonthly, 1, "monthly funding fired once");
    Geoscape_AdvanceTime(&t, 60, &hooks);
    CheckEqI32(g_nMonthly, 1, "monthly funding does not fire twice in the same month");

    /* Month epoch throttle: re-stamped only when (total - stamp) / 5 != 0. */
    GameTime_Reset(&t);
    Geoscape_AdvanceTime(&t, 3, &hooks);
    CheckEqI32(t.nMonthEpochSec, 0, "3 s delta keeps the epoch");
    Geoscape_AdvanceTime(&t, 2, &hooks);
    CheckEqI32(t.nMonthEpochSec, 5, "5 s delta re-stamps the epoch");
}

int main(void)
{
    TestDecode();
    TestReset();
    TestAdvance();
    printf(g_nFailures ? "FAIL %d checks\n" : "PASS (%d failures)\n", g_nFailures);
    return g_nFailures != 0;
}
