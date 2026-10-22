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

/* The original calls its two SetTimers in a fixed order -- the first through a BL, the second through
 * a tail branch -- so the recorded pair IS the function. These two helpers pin one recorded call at
 * one position rather than only its presence. */
static bool SetPairAt(u32 nPos, u32 nId, u32 nMs)
{
    return nPos < g_log.nSets && nPos < LOG_MAX
        && g_log.aSets[nPos].nId == nId && g_log.aSets[nPos].nMs == nMs;
}

static bool KillAt(u32 nPos, u32 nId)
{
    return nPos < g_log.nKills && nPos < LOG_MAX && g_log.aKills[nPos] == nId;
}

/* --- tests ---------------------------------------------------------------- */

static void TestPlainSets(void)
{
    static const char *const apszStart[] = { "StartTimers_A", "StartTimers_B", "StartTimers_C" };
    static const char *const apszKill[]  = { "KillTimers_A", "KillTimers_B", "KillTimers_C" };

    printf("Timer_StartA / StartB / StartC -- three copies of one body\n");

    /* StartTimers_A @ 0x9e6d4: SetTimer(0xC300|0x51, 0x28 = 40 ms) at 0x9E6F0, then POP {R4,LR} at
     * 0x9E708 and the tail `B SetTimer` with (0xC300|0x52, 0x19 = 25 ms) at 0x9E70C.
     * StartTimers_B @ 0x9e714 and StartTimers_C @ 0x9e784 run the same pair (calls at 0x9E730 /
     * 0x9E74C and 0x9E7A0 / 0x9E7BC). A byte compare over the 64 bytes of each shows the three copies
     * differ only at +0x1C and +0x38 -- the two branch words, which encode the distance to the shared
     * SetTimer thunk -- so the ids and the intervals are the same in all three. */
    for (i32 variant = 0; variant < 3; ++variant) {
        LogReset();
        switch (variant) {
            case 0: Timer_StartA(); break;
            case 1: Timer_StartB(); break;
            default: Timer_StartC(); break;
        }
        printf("       (%s)\n", apszStart[variant]);
        CheckEqI32(g_log.nSets, 2, "  sets exactly two timers");
        Check(SetPairAt(0, PUFO_TIMER_RENDER, 40), "  first set is 0xC351 at 40 ms");
        Check(SetPairAt(1, PUFO_TIMER_ANIM, 25), "  second set is 0xC352 at 25 ms (the tail call)");
        CheckEqI32(IntervalFor(PUFO_TIMER_RENDER), 40, "  render interval 40 ms");
        CheckEqI32(IntervalFor(PUFO_TIMER_ANIM), 25, "  anim interval 25 ms");
        Check(IntervalFor(PUFO_TIMER_GAMETICK) == -1, "  no 0xC350 -- this set does not arm the game tick");
        Check(IntervalFor(PUFO_TIMER_EXTRA) == -1, "  no 0xC354 -- that id belongs to StartTimers_D");
    }

    /* KillTimers_A @ 0x9e6a4: KillTimer(0xC351) at 0x9E6B8, then the tail KillTimer(0xC352) at
     * 0x9E6CC. KillTimers_B @ 0x9e754 (0x9E768 / 0x9E77C) and KillTimers_C @ 0x9e7c4 (0x9E7D8 /
     * 0x9E7EC) are the same two kills. Both copies leave 0xC350 and 0xC353 running -- only
     * KillTimers_BySpeed and KillTimers_D touch those -- which the two negative checks below pin. */
    for (i32 variant = 0; variant < 3; ++variant) {
        LogReset();
        switch (variant) {
            case 0: Timer_KillA(); break;
            case 1: Timer_KillB(); break;
            default: Timer_KillC(); break;
        }
        printf("       (%s)\n", apszKill[variant]);
        CheckEqI32(g_log.nKills, 2, "  kills exactly two timers");
        Check(KillAt(0, PUFO_TIMER_RENDER), "  first kill is 0xC351");
        Check(KillAt(1, PUFO_TIMER_ANIM), "  second kill is 0xC352 (the tail call)");
        Check(KilledId(PUFO_TIMER_RENDER), "  kills the render timer");
        Check(KilledId(PUFO_TIMER_ANIM), "  kills the anim timer");
        Check(!KilledId(PUFO_TIMER_GAMETICK), "  does NOT kill 0xC350");
        Check(!KilledId(PUFO_TIMER_UIPULSE), "  does NOT kill 0xC353");
    }
}

static void TestSetD(void)
{
    printf("Timer_StartD / KillD -- the four-id outlier\n");

    /* StartTimers_D @ 0x9e7f4 (112 bytes): SetTimer at 0x9E810 with id 0xC300|0x51 and
     * `MOV R2,#0` at 0x9E808, at 0x9E828 with 0xC352 and 0x32 (50), at 0x9E840 with 0xC353 and 0x14
     * (20), and through the tail branch at 0x9E85C with 0xC354 and 0xA (10). The intervals are this
     * set's own: 20 ms for the pulse here, not the 50 ms the plain set uses. */
    LogReset();
    Timer_StartD();
    CheckEqI32(g_log.nSets, 4, "  sets four timers");
    Check(SetPairAt(0, PUFO_TIMER_RENDER, 0), "  first set is 0xC351 at 0 ms (MOV R2,#0 at 0x9E808)");
    Check(SetPairAt(1, PUFO_TIMER_ANIM, 50), "  second is 0xC352 at 50 ms (0x9E828)");
    Check(SetPairAt(2, PUFO_TIMER_UIPULSE, 20), "  third is 0xC353 at 20 ms (0x9E840)");
    Check(SetPairAt(3, PUFO_TIMER_EXTRA, 10), "  fourth is 0xC354 at 10 ms (the tail at 0x9E85C)");
    /* The zero interval is the original's: Win32 treats it as "as fast as
     * possible", not as "disabled". */
    CheckEqI32(IntervalFor(PUFO_TIMER_RENDER), 0, "  render interval is 0 ms");
    CheckEqI32(IntervalFor(PUFO_TIMER_ANIM), 50, "  anim interval 50 ms");
    CheckEqI32(IntervalFor(PUFO_TIMER_UIPULSE), 20, "  ui pulse 20 ms");
    CheckEqI32(IntervalFor(PUFO_TIMER_EXTRA), 10, "  extra 10 ms");
    Check(IntervalFor(PUFO_TIMER_GAMETICK) == -1, "  StartTimers_D never sets 0xC350");

    /* KillTimers_D @ 0x9e864: KillTimer at 0x9E878 (0xC351), 0x9E888 (0xC352), 0x9E898 (0xC353) and
     * the tail at 0x9E8AC (0xC354). It is exactly StartTimers_D's id set -- 0xC350 survives this call. */
    LogReset();
    Timer_KillD();
    CheckEqI32(g_log.nKills, 4, "  kills the same four ids");
    Check(KillAt(0, PUFO_TIMER_RENDER), "  kills 0xC351 (0x9E878)");
    Check(KillAt(1, PUFO_TIMER_ANIM), "  kills 0xC352 (0x9E888)");
    Check(KillAt(2, PUFO_TIMER_UIPULSE), "  kills 0xC353 (0x9E898)");
    Check(KillAt(3, PUFO_TIMER_EXTRA), "  kills 0xC354 (the tail at 0x9E8AC)");
    Check(KilledId(PUFO_TIMER_RENDER), "  kills render");
    Check(KilledId(PUFO_TIMER_UIPULSE), "  kills ui pulse");
    Check(KilledId(PUFO_TIMER_EXTRA), "  kills the extra id");
    Check(!KilledId(PUFO_TIMER_GAMETICK), "  does NOT kill 0xC350");
}

static void TestBySpeed(void)
{
    printf("Timer_StartBySpeed -- one interval pair per speed\n");

    /* StartTimers_BySpeed @ 0x9e904 reads Map_nGameSpeed at 0x9E918 and dispatches on an UNSIGNED
     * CMP chain (0x9E91C..0x9E940). Speed 0 lands at 0x9E98C, whose SetTimer(0xC350, 0x64) at 0x9E9A0
     * is followed by `MOV R2,#0x64` at 0x9E9A4 and FALLS THROUGH into 0x9E9A8, where SetTimer takes
     * 0xC300|0x51 with that same R2 (the call is at 0x9E9B8); speed 1 uses 0x9E96C with 0x32 (50) at
     * 0x9E980 and `B 0x9E9A8`; speeds 2 and 3 use 0x9E944 with 0xA (10) at 0x9E958, and speed 4 gets
     * there by falling through `CMP R0,#4` / `BNE` at 0x9E93C/0x9E940. From 0x9E9A8 every matched
     * speed falls into 0x9E9BC: SetTimer(0xC352, 0x19 = 25) at 0x9E9D0 and the tail
     * `B SetTimer` with 0xC353 and 0x32 (50) at 0x9E9EC. */
    LogReset();
    Timer_StartBySpeed(0);
    CheckEqI32(IntervalFor(PUFO_TIMER_GAMETICK), 100, "  speed 0 gametick 100 ms");
    CheckEqI32(IntervalFor(PUFO_TIMER_RENDER), 100, "  speed 0 render 100 ms");
    CheckEqI32(IntervalFor(PUFO_TIMER_ANIM), 25, "  speed 0 still sets anim");
    CheckEqI32(g_log.nSets, 4, "  speed 0 sets four timers");
    Check(SetPairAt(0, PUFO_TIMER_GAMETICK, 100), "  order: 0xC350 first (0x9E9A0)");
    Check(SetPairAt(1, PUFO_TIMER_RENDER, 100), "  then 0xC351 with the same interval (0x9E9B8)");
    Check(SetPairAt(2, PUFO_TIMER_ANIM, 25), "  then 0xC352 at 25 ms (0x9E9D0)");
    Check(SetPairAt(3, PUFO_TIMER_UIPULSE, 50), "  then 0xC353 at 50 ms (the tail at 0x9E9EC)");

    LogReset();
    Timer_StartBySpeed(1);
    CheckEqI32(IntervalFor(PUFO_TIMER_GAMETICK), 50, "  speed 1 gametick 50 ms");
    CheckEqI32(IntervalFor(PUFO_TIMER_RENDER), 50, "  speed 1 render 50 ms");
    CheckEqI32(g_log.nSets, 4, "  speed 1 also sets the two trailing timers");
    Check(SetPairAt(0, PUFO_TIMER_GAMETICK, 50), "  speed 1: 0xC350 at 50 ms (0x9E978)");

    /* Speeds 2, 3 and 4 share one arm. */
    for (u32 nSpeed = 2; nSpeed <= 4; ++nSpeed) {
        LogReset();
        Timer_StartBySpeed(nSpeed);
        CheckEqI32(IntervalFor(PUFO_TIMER_GAMETICK), 10, "  speeds 2-4 gametick 10 ms");
        CheckEqI32(IntervalFor(PUFO_TIMER_RENDER), 10, "  speeds 2-4 render 10 ms");
        CheckEqI32(g_log.nSets, 4, "  speeds 2-4 set four timers too");
    }
    /* Speed 4 is not its own arm: `CMP R0,#4` at 0x9E93C is followed by `BNE 0x9E9BC` at 0x9E940, so
     * the equal case falls through into the 2/3 block. It answers exactly like 2 and 3, checked above;
     * what distinguishes it is that a missing fall-through would make 4 behave like 5+. */
    LogReset();
    Timer_StartBySpeed(4);
    Check(SetPairAt(0, PUFO_TIMER_GAMETICK, 10), "  speed 4 reaches the 2/3 block (fall-through at 0x9E940)");

    /* Speed 5+ sets NEITHER game-rate timer, but still sets the two trailing
     * ones. This asymmetry is the original's missing default arm: `BNE 0x9E9BC` at 0x9E940 jumps
     * straight to the trailing block, and 0x9E9BC is the only block it reaches. */
    LogReset();
    Timer_StartBySpeed(5);
    CheckEqI32(IntervalFor(PUFO_TIMER_GAMETICK), -1, "  speed 5 sets no gametick timer");
    CheckEqI32(IntervalFor(PUFO_TIMER_RENDER), -1, "  speed 5 sets no render timer");
    CheckEqI32(IntervalFor(PUFO_TIMER_ANIM), 25, "  speed 5 still sets anim");
    CheckEqI32(IntervalFor(PUFO_TIMER_UIPULSE), 50, "  speed 5 still sets ui pulse");
    CheckEqI32(g_log.nSets, 2, "  speed 5 sets exactly two timers");
    Check(SetPairAt(0, PUFO_TIMER_ANIM, 25), "  speed 5 starts at 0x9E9BC: anim is the FIRST set");

    /* The dispatch chain compares UNSIGNED (CMP R0,#0 through CMP R0,#4 with no signed branch), so a
     * speed that arrived as a negative int reads as a huge unsigned value and takes the same
     * BNE-to-0x9E9BC path as 5. 0xFFFFFFFF is that value, and it must not reach any game-rate arm. */
    LogReset();
    Timer_StartBySpeed(0xFFFFFFFFu);
    CheckEqI32(g_log.nSets, 2, "  a wrapped speed sets exactly the two trailing timers");
    CheckEqI32(IntervalFor(PUFO_TIMER_GAMETICK), -1, "  a wrapped speed sets no gametick timer");
    CheckEqI32(IntervalFor(PUFO_TIMER_RENDER), -1, "  a wrapped speed sets no render timer");

    /* KillTimers_BySpeed @ 0x9e8b4: KillTimer(0xC350) at 0x9E8C8, 0xC351 at 0x9E8D8, 0xC352 at
     * 0x9E8E8, and the tail `B KillTimer` with 0xC353 at 0x9E8FC. Unconditional -- it kills 0xC350
     * even when the speed-driven start never set it -- and 0xC354 is NOT in the list. */
    LogReset();
    Timer_KillBySpeed();
    CheckEqI32(g_log.nKills, 4, "  KillBySpeed kills four ids");
    Check(KillAt(0, PUFO_TIMER_GAMETICK), "  first kill is 0xC350 (0x9E8C8)");
    Check(KillAt(1, PUFO_TIMER_RENDER), "  then 0xC351 (0x9E8D8)");
    Check(KillAt(2, PUFO_TIMER_ANIM), "  then 0xC352 (0x9E8E8)");
    Check(KillAt(3, PUFO_TIMER_UIPULSE), "  then 0xC353 (the tail at 0x9E8FC)");
    Check(KilledId(PUFO_TIMER_GAMETICK), "  kills the gametick id");
    Check(KilledId(PUFO_TIMER_ANIM), "  kills the anim id");
    Check(KilledId(PUFO_TIMER_UIPULSE), "  kills the ui pulse id");
    Check(!KilledId(PUFO_TIMER_EXTRA), "  does NOT kill 0xC354 -- only KillTimers_D does");
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
    i32 nTickUnit;
    i32 nClearScratch;
    i32 nRender;
} TickLog;

static TickLog g_tick;

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

static void H_RenderFrame(void *pUser)
{
    (void)pUser;
    g_tick.nRender += 1; /* GameTick_Step's wrap tail Render_Frame @ 0x274C4 */
}

static const GameTickHooks g_tickHooks = {
    H_TickActiveUnit, H_ClearScratch, H_RenderFrame, NULL
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
    u8           bSlot8;  /* the slot-8 byte and its pair are now POINTERS in the ctx (Part 105):
                           * the 0x275CC gate reads the byte after the step, which may clear it */
    u8           bEnemy;

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

    /* Gate 2: the geoscape path runs the original's EMPTY stub and returns. 0x27514 loads the geoscape
     * screen block, 0x27518 calls Geoscape_GameTickStub @ 0x94550 -- four bytes, `BX LR`, no body -- and
     * 0x2751C returns. The geoscape clock is 0xC353's UiPulse_OnTimer, not this handler's job. */
    TickCtxClear(&ctx);
    ctx.bGeoscapeUi = true;
    TickLogReset();
    Check(GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL),
          "  geoscape path took its own branch");
    CheckEqI32(g_tick.nTickUnit, 0, "  no active-unit tick on it");
    CheckEqI32(g_tick.nClearScratch, 0, "  and no frame-scratch clear -- the stub has no body");

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

    /* TickActiveUnit is skipped only when the phase is active AND slot8 byte 1 is set -- and the
     * original reads that byte LIVE at 0x275CC, after the 0x275b0 call into GameTick_Step. */
    TickCtxClear(&ctx);
    state.bPhaseActive = 1;
    ctx.pbSlot8Byte1 = &bSlot8;
    ctx.pbEnemyTurnActive = &bEnemy;
    bSlot8 = 1; bEnemy = 0;
    TickLogReset();
    (void)GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL);
    CheckEqI32(g_tick.nTickUnit, 0, "  phase active + slot8 -> unit tick SKIPPED");
    CheckEqI32(g_tick.nClearScratch, 1, "  but the scratch still clears");
    CheckEqI32(bEnemy, 1, "  and the step armed Map_bEnemyTurnActive from the slot-8 byte (0x27364/0x27388)");
    CheckEqI32(g_tick.nRender, 0, "  a non-wrap step renders nothing");

    /* Either condition alone still ticks the unit. */
    bSlot8 = 0;
    TickLogReset();
    (void)GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL);
    CheckEqI32(g_tick.nTickUnit, 1, "  phase active alone still ticks the unit");

    bSlot8 = 1;
    state.bPhaseActive = 0;
    TickLogReset();
    (void)GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL);
    CheckEqI32(g_tick.nTickUnit, 1, "  slot8 alone still ticks the unit");

    /* The wrap tick (Part 105): GameTick_Step's 12th tick clears the trigger byte at 0x274B4, so
     * the live 0x275CC read sees ZERO and this tick still ticks the unit -- a value copy captured
     * at entry would wrongly skip it. The wrap also clears the enemy byte and tail-renders. */
    TickCtxClear(&ctx);
    TactState_ctor(&state);
    ctx.pbSlot8Byte1 = &bSlot8;
    ctx.pbEnemyTurnActive = &bEnemy;
    state.bPhaseActive = 1;
    state.nPhaseCounter = PUFO_PHASE_TICK_MAX;  /* this step is tick 12 */
    bSlot8 = 1; bEnemy = 1;
    TickLogReset();
    Check(GameTick_OnTimer(&ctx, &stack, &state, &g_tickHooks, Tick_NoCells, NULL),
          "  wrap tick ran");
    CheckEqI32(state.nPhaseCounter, PUFO_PHASE_TICK_MAX + 1, "  counter passed 11");
    CheckEqI32(state.bPhaseActive, 0, "  0x274AC cleared the phase");
    CheckEqI32(bSlot8, 0, "  0x274B4 cleared unk_113A39");
    CheckEqI32(bEnemy, 0, "  0x274BC cleared Map_bEnemyTurnActive");
    CheckEqI32(g_tick.nRender, 1, "  0x274C4 tail Render_Frame ran once");
    CheckEqI32(g_tick.nTickUnit, 1, "  the LIVE 0x275CC read sees the cleared byte: the wrap tick still ticks");

    /* NULL safety. */
    Check(!GameTick_OnTimer(NULL, &stack, &state, &g_tickHooks, Tick_NoCells, NULL),
          "  NULL ctx -> false");
    Check(!GameTick_OnTimer(&ctx, NULL, &state, &g_tickHooks, Tick_NoCells, NULL),
          "  NULL stack -> false");
    Check(!GameTick_OnTimer(&ctx, &stack, NULL, &g_tickHooks, Tick_NoCells, NULL),
          "  NULL state -> false");
}

/* --- the 0xC350 dispatch rule (PocketUFO_WndProc 0xA08C0..0xA0934) ------------ */

static i32 g_nDispatchTicks;

static void Dispatch_Tick(void *pUser)
{
    (void)pUser;
    g_nDispatchTicks += 1;
}

static void TestTickBySpeed(void)
{
    printf("Timer_TickBySpeed -- how many ticks one 0xC350 firing runs\n");

    g_nDispatchTicks = 0;

    Timer_TickBySpeed(-1, Dispatch_Tick, NULL);
    CheckEqI32(g_nDispatchTicks, 0, "  a negative speed ticks nothing (CMP #0 / BMI 0xA08C8)");

    Timer_TickBySpeed(5, Dispatch_Tick, NULL);
    CheckEqI32(g_nDispatchTicks, 0, "  a speed above 4 ticks nothing (BNE 0xA08E0)");

    Timer_TickBySpeed(0, Dispatch_Tick, NULL);
    CheckEqI32(g_nDispatchTicks, 1, "  speed 0 ticks once (BLE 0xA08D0 -> 0xA08F4/0xA08F8)");

    Timer_TickBySpeed(1, Dispatch_Tick, NULL);
    Timer_TickBySpeed(2, Dispatch_Tick, NULL);
    CheckEqI32(g_nDispatchTicks, 3, "  speeds 1 and 2 tick once each");

    Timer_TickBySpeed(4, Dispatch_Tick, NULL);
    CheckEqI32(g_nDispatchTicks, 6, "  speed 4 ticks three times (0xA08E8, 0xA08F0, then 0xA08F8)");

    /* Speed 3 alternates on bGameTickExtraDue @ 0x18838C, which starts 0: the firings run 1, 2, 1. */
    Timer_TickBySpeed(3, Dispatch_Tick, NULL);
    CheckEqI32(g_nDispatchTicks, 7, "  speed 3, first firing: one tick, the extra becomes due (0xA092C)");

    Timer_TickBySpeed(3, Dispatch_Tick, NULL);
    CheckEqI32(g_nDispatchTicks, 9, "  speed 3, second firing: the extra runs (0xA091C) and the flag clears");

    Timer_TickBySpeed(3, Dispatch_Tick, NULL);
    CheckEqI32(g_nDispatchTicks, 10, "  speed 3, third firing: back to one tick");

    /* Only speed 3 reads or writes the flag: an interleaved speed-2 firing leaves it set, so the next
     * speed-3 firing still runs its extra (11 from the speed-2 tick, +2 from this one). */
    Timer_TickBySpeed(2, Dispatch_Tick, NULL);
    Timer_TickBySpeed(3, Dispatch_Tick, NULL);
    CheckEqI32(g_nDispatchTicks, 13, "  the other speeds leave bGameTickExtraDue alone");
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
    TestTickBySpeed();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
