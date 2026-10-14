/* pufo_clock.h - the geoscape game clock.
 *
 * Evidence: GameTime_Decode @ 0x5cfd4, GameTime_Reset @ 0x5c56c,
 * Geoscape_AdvanceTime @ 0x5ce40. The original keeps the whole block inside
 * the big global at 0x158890 (IDA's gUiRootPanel.apLines[101..115]); the IDB
 * type GameTime (60 bytes) sits at 0x158A14 and every field below is one of
 * those apLines slots.
 *
 * Month lengths come from dword_AB270 @ 0xab270, read as 12 dwords:
 * 31,28,31,30,31,30,31,31,30,31,30,31 (a fixed non-leap year).
 */
#ifndef PUFO_CLOCK_H
#define PUFO_CLOCK_H

#include "pufo_common.h"

typedef struct GameTime {
    i32 nLastTickMinSec;  /* apLines[101]  last minute-tick stamp  */
    i32 nLastTickHourSec; /* apLines[102]  last hour-tick stamp    */
    i32 nLastTickDaySec;  /* apLines[103]  last day-tick stamp     */
    i32 nUnkC;            /* apLines[104]  never read or written   */
    i32 nTotalSec;        /* apLines[105]  seconds since game start*/
    u8  bPaused;          /* apLines[106]  GameTime_Pause flag     */
    u8  gap_15[3];
    i32 nYear;            /* apLines[107]  1999 + total/31536000   */
    i32 nMonth;           /* apLines[108]  1..12                   */
    i32 nDay;             /* apLines[109]  1..31                   */
    i32 nWeekday;         /* apLines[110]  1..7                    */
    i32 nHour;            /* apLines[111]  0..23                   */
    i32 nMinute;          /* apLines[112]  0..59                   */
    i32 nSecond;          /* apLines[113]  0..59                   */
    i32 nUnk34;           /* apLines[114]  cleared by Reset only   */
    i32 nMonthEpochSec;   /* apLines[115]  5-second throttle stamp */
} GameTime;

_Static_assert(sizeof(GameTime) == 60, "GameTime must be 60 bytes");

/* Seconds per game month lengths table (dword_AB270). */
#define GAME_SECONDS_YEAR   31536000
#define GAME_SECONDS_DAY    86400
#define GAME_SECONDS_HOUR   3600
#define GAME_SECONDS_MINUTE 60

/* GameTime_Decode @ 0x5cfd4. Splits nTotalSec into year / month / day /
 * weekday / time-of-day and writes them back into the struct. Returns the
 * seconds remainder (the original returns nSecond). */
i32 GameTime_Decode(GameTime *pTime);

/* GameTime_Reset @ 0x5c56c. Clears the pause flag, the total, the three tick
 * stamps, nUnk34 and the month epoch, then decodes. */
i32 GameTime_Reset(GameTime *pTime);

/* Geoscape_AdvanceTime @ 0x5ce40 drives the whole geoscape from the clock.
 * The tick callbacks stand for Geoscape_TickShips / Geoscape_TickHourly /
 * Geoscape_TickBases / FundingScreen_OpenMonthly, which live in other
 * modules; the port injects them so the clock is testable on its own. */
typedef struct GeoscapeClockHooks {
    void (*pfnTickShips)(void *pUser);
    void (*pfnTickHourly)(void *pUser);
    void (*pfnTickBases)(void *pUser);
    void (*pfnOpenMonthly)(void *pUser);
    void *pUser;
} GeoscapeClockHooks;

/* Advance the clock by nDeltaSec and run the due ticks.
 *
 * Faithful quirks of the original:
 * - the minute loop is skipped while bPaused, the hour and day loops are not;
 * - a negative delta updates nothing but is still divided (ARM truncates
 *   toward zero, same as C99);
 * - the monthly funding screen opens when the decoded month differs from the
 *   month read at entry, i.e. the tick chain itself changed it;
 * - nMonthEpochSec is re-stamped whenever (total - stamp) / 5 != 0. */
void Geoscape_AdvanceTime(GameTime *pTime, i32 nDeltaSec,
                          const GeoscapeClockHooks *pHooks);

/* GameTime_Unpause @ 0x5d6c0. One write: it clears the pause byte, the same
 * apLines[106] byte GameTime_Pause @ 0x5d6ac sets. There is no counter and no other
 * state, so unpausing while already running is a no-op rather than an error. */
void GameTime_Unpause(GameTime *pTime);

#endif /* PUFO_CLOCK_H */
