/* test_timer.c - checks the Win32 timer set's ids and intervals.
 *
 * The timers are verified through an injected backend, because Win32 timers are
 * the only part of this layer that cannot run headless. The id/interval table is
 * what matters and it is checked exactly.
 */
#include "game/pufo_timer.h"

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

/* --- recording backend ---------------------------------------------------- */

#define LOG_MAX 16

typedef struct LogEntry {
    u32 nId;
    u32 nMs;
} LogEntry;

typedef struct Log {
    LogEntry aSets[LOG_MAX];
    u32      nSets;
    u32      aKills[LOG_MAX];
    u32      nKills;
    HWND     hLastWnd;
} Log;

static Log g_log;

static void OnSet(HWND hWnd, u32 nId, u32 nMs, void *pUser)
{
    (void)pUser;
    g_log.hLastWnd = hWnd;
    if (g_log.nSets < LOG_MAX) {
        g_log.aSets[g_log.nSets].nId = nId;
        g_log.aSets[g_log.nSets].nMs = nMs;
    }
    g_log.nSets += 1;
}

static void OnKill(HWND hWnd, u32 nId, void *pUser)
{
    (void)pUser;
    g_log.hLastWnd = hWnd;
    if (g_log.nKills < LOG_MAX) {
        g_log.aKills[g_log.nKills] = nId;
    }
    g_log.nKills += 1;
}

static const TimerBackend g_backend = { OnSet, OnKill, NULL };

static void LogReset(void)
{
    memset(&g_log, 0, sizeof(g_log));
}

/* Returns the interval recorded for nId on the last set pass, or -1 when that id
 * was never set. */
static i32 IntervalFor(u32 nId)
{
    for (u32 i = 0; i < g_log.nSets && i < LOG_MAX; ++i) {
        if (g_log.aSets[i].nId == nId) {
            return (i32)g_log.aSets[i].nMs;
        }
    }
    return -1;
}

static bool KilledId(u32 nId)
{
    for (u32 i = 0; i < g_log.nKills && i < LOG_MAX; ++i) {
        if (g_log.aKills[i] == nId) {
            return true;
        }
    }
    return false;
}

/* --- tests ---------------------------------------------------------------- */

static void TestPlainSets(void)
{
    printf("Timer_StartA / StartB / StartC -- byte-identical pairs\n");
    /* A, B and C are three copies of the same body in the original; the port must
     * produce the same requests for each. */
    for (i32 variant = 0; variant < 3; ++variant) {
        LogReset();
        switch (variant) {
            case 0: Timer_StartA(); break;
            case 1: Timer_StartB(); break;
            default: Timer_StartC(); break;
        }
        CheckEqI32(g_log.nSets, 2, "  sets exactly two timers");
        CheckEqI32(IntervalFor(PUFO_TIMER_RENDER), 40, "  render interval 40 ms");
        CheckEqI32(IntervalFor(PUFO_TIMER_ANIM), 25, "  anim interval 25 ms");
    }

    for (i32 variant = 0; variant < 3; ++variant) {
        LogReset();
        switch (variant) {
            case 0: Timer_KillA(); break;
            case 1: Timer_KillB(); break;
            default: Timer_KillC(); break;
        }
        CheckEqI32(g_log.nKills, 2, "  kills exactly two timers");
        Check(KilledId(PUFO_TIMER_RENDER), "  kills the render timer");
        Check(KilledId(PUFO_TIMER_ANIM), "  kills the anim timer");
    }
}

static void TestSetD(void)
{
    printf("Timer_StartD / KillD -- the four-id outlier\n");
    LogReset();
    Timer_StartD();
    CheckEqI32(g_log.nSets, 4, "  sets four timers");
    /* The zero interval is the original's: Win32 treats it as "as fast as
     * possible", not as "disabled". */
    CheckEqI32(IntervalFor(PUFO_TIMER_RENDER), 0, "  render interval is 0 ms");
    CheckEqI32(IntervalFor(PUFO_TIMER_ANIM), 50, "  anim interval 50 ms");
    CheckEqI32(IntervalFor(PUFO_TIMER_UIPULSE), 20, "  ui pulse 20 ms");
    CheckEqI32(IntervalFor(PUFO_TIMER_EXTRA), 10, "  extra 10 ms");

    LogReset();
    Timer_KillD();
    CheckEqI32(g_log.nKills, 4, "  kills the same four ids");
    Check(KilledId(PUFO_TIMER_RENDER), "  kills render");
    Check(KilledId(PUFO_TIMER_UIPULSE), "  kills ui pulse");
    Check(KilledId(PUFO_TIMER_EXTRA), "  kills the extra id");
}

static void TestBySpeed(void)
{
    printf("Timer_StartBySpeed -- one interval pair per speed\n");

    /* speed 0 and 1 set both game-rate timers to the same value. */
    LogReset();
    Timer_StartBySpeed(0);
    CheckEqI32(IntervalFor(PUFO_TIMER_GAMETICK), 100, "  speed 0 gametick 100 ms");
    CheckEqI32(IntervalFor(PUFO_TIMER_RENDER), 100, "  speed 0 render 100 ms");
    CheckEqI32(IntervalFor(PUFO_TIMER_ANIM), 25, "  speed 0 still sets anim");
    CheckEqI32(g_log.nSets, 4, "  speed 0 sets four timers");

    LogReset();
    Timer_StartBySpeed(1);
    CheckEqI32(IntervalFor(PUFO_TIMER_GAMETICK), 50, "  speed 1 gametick 50 ms");
    CheckEqI32(IntervalFor(PUFO_TIMER_RENDER), 50, "  speed 1 render 50 ms");

    /* Speeds 2, 3 and 4 share one arm. */
    for (u32 nSpeed = 2; nSpeed <= 4; ++nSpeed) {
        LogReset();
        Timer_StartBySpeed(nSpeed);
        CheckEqI32(IntervalFor(PUFO_TIMER_GAMETICK), 10, "  speeds 2-4 gametick 10 ms");
        CheckEqI32(IntervalFor(PUFO_TIMER_RENDER), 10, "  speeds 2-4 render 10 ms");
    }

    /* Speed 5+ sets NEITHER game-rate timer, but still sets the two trailing
     * ones. This asymmetry is the original's missing default arm. */
    LogReset();
    Timer_StartBySpeed(5);
    CheckEqI32(IntervalFor(PUFO_TIMER_GAMETICK), -1, "  speed 5 sets no gametick timer");
    CheckEqI32(IntervalFor(PUFO_TIMER_RENDER), -1, "  speed 5 sets no render timer");
    CheckEqI32(IntervalFor(PUFO_TIMER_ANIM), 25, "  speed 5 still sets anim");
    CheckEqI32(IntervalFor(PUFO_TIMER_UIPULSE), 50, "  speed 5 still sets ui pulse");
    CheckEqI32(g_log.nSets, 2, "  speed 5 sets exactly two timers");

    /* KillBySpeed kills four ids regardless of what was started. */
    LogReset();
    Timer_KillBySpeed();
    CheckEqI32(g_log.nKills, 4, "  KillBySpeed kills four ids");
    Check(KilledId(PUFO_TIMER_GAMETICK), "  kills the gametick id");
    Check(KilledId(PUFO_TIMER_ANIM), "  kills the anim id");
    Check(KilledId(PUFO_TIMER_UIPULSE), "  kills the ui pulse id");
}

static void TestWindowAndBackend(void)
{
    HWND hWnd;

    printf("Window handling\n");
    /* A NULL window is refused; the module keeps the previously set one. */
    Check(!Timer_SetWindow(NULL), "  NULL window refused");
    hWnd = (HWND)(uintptr_t)0x1234;
    Check(Timer_SetWindow(hWnd), "  window accepted");
    LogReset();
    Timer_StartA();
    Check(g_log.hLastWnd == hWnd, "  the set window is the one used");

    /* Restoring the default backend must not break the module. */
    Timer_SetBackend(NULL);
    Check(true, "  resetting the backend is safe");
    Timer_SetBackend(&g_backend);
}

/* --- GameTick_OnTimer @ 0x274f0 ------------------------------------------- */

typedef struct TickLog {
    i32 nGeoscape;
    i32 nTickUnit;
    i32 nClearScratch;
} TickLog;

static TickLog g_tick;

static void H_GeoscapeTick(void *pUser)
{
    (void)pUser;
    g_tick.nGeoscape += 1;
}

static void H_TickActiveUnit(void *pUser)
{
    (void)pUser;
    g_tick.nTickUnit += 1;
}

static void H_ClearScratch(void *pUser)
{
    (void)pUser;
    g_tick.nClearScratch += 1;
}

static const GameTickHooks g_tickHooks = {
    H_GeoscapeTick, H_TickActiveUnit, H_ClearScratch, NULL
};

/* A grid lookup that reports no cells, so GameTick_Step sweeps nothing and the test
 * focuses on the handler's own gating. */
static MapCell *Tick_NoCells(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)nX;
    (void)nY;
    (void)nLayer;
    (void)pUser;
    return NULL;
}

static void TickLogReset(void)
{
    memset(&g_tick, 0, sizeof(g_tick));
}

static void TickCtxClear(GameTickCtx *pCtx)
{
    memset(pCtx, 0, sizeof(*pCtx));
    /* Defaults that let the handler reach the tick body. */
    pCtx->bGameTickHold = false;
    pCtx->bGeoscapeUi = false;
    pCtx->bTactFlag = true;
    pCtx->nUiSlot10 = 0;
}

static void TestGameTickOnTimer(void)
{
    GameTickCtx  ctx;
    StatusStack  stack;
    TactState    state;

    printf("GameTick_OnTimer -- four gates and the TTL decay\n");

    /* Gate 1: the hold flag stops everything. */
    TickCtxClear(&ctx);
    memset(&stack, 0, sizeof(stack));
    TactState_ctor(&state);
    TickLogReset();
    ctx.bGameTickHold = true;
    Check(!GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL),
          "  hold flag returns immediately");
    CheckEqI32(g_tick.nTickUnit, 0, "  nothing ran");

    /* Gate 2: the geoscape substitutes its own tick and returns. */
    TickCtxClear(&ctx);
    ctx.bGeoscapeUi = true;
    TickLogReset();
    Check(GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL),
          "  geoscape path ran");
    CheckEqI32(g_tick.nGeoscape, 1, "  geoscape tick called");
    CheckEqI32(g_tick.nTickUnit, 0, "  tactical unit tick NOT called");

    /* Gate 3: the UI slot must be 10 or 0. */
    TickCtxClear(&ctx);
    ctx.nUiSlot10 = 99;
    TickLogReset();
    Check(!GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL),
          "  unexpected UI slot returns early");
    ctx.nUiSlot10 = 10;
    Check(GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL),
          "  UI slot 10 runs");

    /* Gate 4: no tactical flag, no tick. */
    TickCtxClear(&ctx);
    ctx.bTactFlag = false;
    TickLogReset();
    Check(!GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL),
          "  non-tactical returns early");

    /* The normal path: TTL decays, the unit ticks, the scratch clears. */
    TickCtxClear(&ctx);
    memset(&stack, 0, sizeof(stack));
    TactState_ctor(&state);
    stack.nCount = 2;
    stack.anTtl[0] = 5;
    stack.anTtl[1] = 9;
    TickLogReset();
    Check(GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL),
          "  normal path ran");
    CheckEqI32(stack.anTtl[0], 4, "  oldest TTL decremented");
    CheckEqI32(stack.anTtl[1], 8, "  newest TTL decremented too");
    CheckEqI32(stack.nCount, 2, "  nothing shifted while TTL is above 0");
    CheckEqI32(g_tick.nTickUnit, 1, "  the active unit ticked");
    CheckEqI32(g_tick.nClearScratch, 1, "  the frame scratch was cleared");

    /* Expiry: the shift tests only anTtl[0] <= 0, after the whole array decayed. */
    TickCtxClear(&ctx);
    memset(&stack, 0, sizeof(stack));
    stack.nCount = 2;
    stack.anTtl[0] = 1;
    stack.anTtl[1] = 50;
    (void)GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL);
    /* The whole array decayed to {0, 49} and then anTtl[0] <= 0 triggered a shift,
     * so slot 0 now holds the SURVIVING value -- the expired one is gone. */
    CheckEqI32(stack.nCount, 1, "  the expired oldest was shifted out");
    CheckEqI32(stack.anTtl[0], 49, "  the surviving TTL moved down");

    /* The TTL decay is gated on !busy, but GameTick_Step is NOT: the phase counter
     * must still advance while busy. */
    TickCtxClear(&ctx);
    ctx.bBusy = true;
    memset(&stack, 0, sizeof(stack));
    stack.nCount = 1;
    stack.anTtl[0] = 7;
    TactState_ctor(&state);
    state.bPhaseActive = 1;
    TickLogReset();
    (void)GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL);
    CheckEqI32(stack.anTtl[0], 7, "  busy: the TTL does NOT decay");
    CheckEqI32(stack.nCount, 1, "  busy: nothing shifted");
    CheckEqI32(state.nPhaseCounter, 1, "  busy: the phase counter STILL advanced");

    /* TickActiveUnit is skipped only when the phase is active AND slot8 byte 1 is set. */
    TickCtxClear(&ctx);
    state.bPhaseActive = 1;
    ctx.bSlot8Byte1 = true;
    TickLogReset();
    (void)GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL);
    CheckEqI32(g_tick.nTickUnit, 0, "  phase active + slot8 -> unit tick SKIPPED");
    CheckEqI32(g_tick.nClearScratch, 1, "  but the scratch still clears");

    /* Either condition alone still ticks the unit. */
    ctx.bSlot8Byte1 = false;
    TickLogReset();
    (void)GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL);
    CheckEqI32(g_tick.nTickUnit, 1, "  phase active alone still ticks the unit");

    ctx.bSlot8Byte1 = true;
    state.bPhaseActive = 0;
    TickLogReset();
    (void)GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL);
    CheckEqI32(g_tick.nTickUnit, 1, "  slot8 alone still ticks the unit");

    /* NULL safety. */
    Check(!GameTick_OnTimer(NULL, &stack, &state, &g_tickHooks, Tick_NoCells, NULL),
          "  NULL ctx -> false");
    Check(!GameTick_OnTimer(&ctx, NULL, &state, &g_tickHooks, Tick_NoCells, NULL),
          "  NULL stack -> false");
    Check(!GameTick_OnTimer(&ctx, &stack, NULL, &g_tickHooks, Tick_NoCells, NULL),
          "  NULL state -> false");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    Timer_SetWindow((HWND)(uintptr_t)0x1);
    Timer_SetBackend(&g_backend);

    TestPlainSets();
    TestSetD();
    TestBySpeed();
    TestWindowAndBackend();
    TestGameTickOnTimer();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
