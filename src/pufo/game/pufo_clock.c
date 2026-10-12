/* pufo_clock.c - see pufo_clock.h for the evidence map. */
#include "game/pufo_clock.h"

/* dword_AB270 @ 0xab270, read as 12 dwords from the IDB. */
static const i32 s_anDaysPerMonth[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

i32 GameTime_Decode(GameTime *pTime)
{
    i32 nTotal = pTime->nTotalSec;
    i32 nYearIdx = nTotal / GAME_SECONDS_YEAR;
    i32 nRemYear = nTotal - GAME_SECONDS_YEAR * nYearIdx;
    i32 nMonthSec = 0;
    i32 nMonthIdx = 0;

    /* Month walk, exactly as the original: add a whole month, stop when the
     * running total passes the remainder, then undo that last month. If all
     * twelve are consumed the total stays as-is. */
    for (;;) {
        nMonthSec += GAME_SECONDS_DAY * s_anDaysPerMonth[nMonthIdx];
        if (nMonthSec > nRemYear)
            break;
        ++nMonthIdx;
        if (nMonthIdx >= 12)
            break;
    }
    if (nMonthIdx < 12)
        nMonthSec -= GAME_SECONDS_DAY * s_anDaysPerMonth[nMonthIdx];

    i32 nRemMonth = nRemYear - nMonthSec;
    i32 nDayIdx = nRemMonth / GAME_SECONDS_DAY;
    i32 nRemDay = nRemMonth - GAME_SECONDS_DAY * nDayIdx;
    i32 nHour = nRemDay / GAME_SECONDS_HOUR;
    nRemDay %= GAME_SECONDS_HOUR;

    pTime->nYear    = nYearIdx + 1999;
    pTime->nMonth   = nMonthIdx + 1;
    pTime->nDay     = nDayIdx + 1;
    /* Weekday base: the original seeds 1999-01-01 so that day 0 is Friday. */
    pTime->nWeekday = (nRemYear / GAME_SECONDS_DAY + 4) % 7 + 1;
    pTime->nHour    = nHour;
    pTime->nMinute  = nRemDay / GAME_SECONDS_MINUTE;
    pTime->nSecond  = nRemDay % GAME_SECONDS_MINUTE;
    return pTime->nSecond;
}

i32 GameTime_Reset(GameTime *pTime)
{
    pTime->bPaused = 0;
    pTime->nTotalSec = 0;
    pTime->nUnk34 = 0;
    pTime->nLastTickDaySec = 0;
    pTime->nLastTickHourSec = 0;
    pTime->nLastTickMinSec = 0;
    pTime->nMonthEpochSec = 0;
    return GameTime_Decode(pTime);
}

void Geoscape_AdvanceTime(GameTime *pTime, i32 nDeltaSec,
                          const GeoscapeClockHooks *pHooks)
{
    i32 nMonthBefore = pTime->nMonth;   /* apLines[116] = apLines[108] */

    pTime->nTotalSec += nDeltaSec;
    GameTime_Decode(pTime);

    i32 nTicks = (pTime->nTotalSec - pTime->nLastTickMinSec) / GAME_SECONDS_MINUTE;
    if (nTicks != 0) {
        if (nTicks > 0) {
            do {
                if (!pTime->bPaused)
                    pHooks->pfnTickShips(pHooks->pUser);
            } while (--nTicks != 0);
        }
        pTime->nLastTickMinSec = pTime->nTotalSec;
    }

    nTicks = (pTime->nTotalSec - pTime->nLastTickHourSec) / GAME_SECONDS_HOUR;
    if (nTicks != 0) {
        if (nTicks > 0) {
            do {
                pHooks->pfnTickHourly(pHooks->pUser);
            } while (--nTicks != 0);
        }
        pTime->nLastTickHourSec = pTime->nTotalSec;
    }

    nTicks = (pTime->nTotalSec - pTime->nLastTickDaySec) / GAME_SECONDS_DAY;
    if (nTicks != 0) {
        if (nTicks > 0) {
            do {
                pHooks->pfnTickBases(pHooks->pUser);
            } while (--nTicks != 0);
        }
        pTime->nLastTickDaySec = pTime->nTotalSec;
    }

    if ((u32)(pTime->nTotalSec - pTime->nMonthEpochSec) / 5u != 0u)
        pTime->nMonthEpochSec = pTime->nTotalSec;

    if (pTime->nMonth != nMonthBefore)
        pHooks->pfnOpenMonthly(pHooks->pUser);
}

void GameTime_Unpause(GameTime *pTime)
{
    if (pTime == NULL) {
        return;
    }
    pTime->bPaused = 0; /* 0x5d6c8: LOBYTE(gUiRootPanel.apLines[106]) = 0 */
}
