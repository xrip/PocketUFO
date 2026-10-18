/* pufo_timer.c - the game's Win32 timer set.
 *
 * Evidence: see pufo_timer.h for the address list and the verified intervals.
 */
#include "game/pufo_timer.h"

#include "game/pufo_tactctl.h"

/* The original's hWndMain @ 0x188d54, which pufo_app.c already owns as
 * g_hWndMain. Timer_SetWindow makes the dependency explicit instead of reading
 * another module's static. */
static HWND s_hWnd = NULL;

bool Timer_SetWindow(HWND hWnd)
{
    if (hWnd == NULL) {
        return false;
    }
    s_hWnd = hWnd;
    return true;
}

/* The real Win32 calls. SetTimer with a NULL timer proc posts WM_TIMER to the
 * window, which is what the original does for every one of these calls. */
static void Backend_SetReal(HWND hWnd, u32 nId, u32 nMs, void *pUser)
{
    (void)pUser;
    (void)SetTimer(hWnd, nId, nMs, NULL);
}

static void Backend_KillReal(HWND hWnd, u32 nId, void *pUser)
{
    (void)pUser;
    (void)KillTimer(hWnd, nId);
}

static TimerBackend s_backend = { Backend_SetReal, Backend_KillReal, NULL };

void Timer_SetBackend(const TimerBackend *pBackend)
{
    if (pBackend != NULL && pBackend->pfnSet != NULL && pBackend->pfnKill != NULL) {
        s_backend = *pBackend;
    } else {
        s_backend.pfnSet  = Backend_SetReal;
        s_backend.pfnKill = Backend_KillReal;
        s_backend.pUser   = NULL;
    }
}

static void Timer_StartOne(u32 nId, u32 nMs)
{
    s_backend.pfnSet(s_hWnd, nId, nMs, s_backend.pUser);
}

static void Timer_KillOne(u32 nId)
{
    s_backend.pfnKill(s_hWnd, nId, s_backend.pUser);
}

/* --------------------------------------------------------------- the A/B/C set */

/* KillTimers_A/B/C @ 0x9e6a4/0x9e754/0x9e7c4 are byte-identical: kill the render
 * and animation timers. The three names are kept because the original has three,
 * and the call sites are recognisable by them. */
void Timer_KillA(void)
{
    Timer_KillOne(PUFO_TIMER_RENDER);
    Timer_KillOne(PUFO_TIMER_ANIM);
}

void Timer_KillB(void) { Timer_KillA(); }
void Timer_KillC(void) { Timer_KillA(); }

/* StartTimers_A/B/C @ 0x9e6d4/0x9e714/0x9e784 are also byte-identical:
 * 0xC351 = 40 ms, 0xC352 = 25 ms. */
void Timer_StartA(void)
{
    Timer_StartOne(PUFO_TIMER_RENDER, PUFO_TIMER_MS_RENDER);
    Timer_StartOne(PUFO_TIMER_ANIM, PUFO_TIMER_MS_ANIM);
}

void Timer_StartB(void) { Timer_StartA(); }
void Timer_StartC(void) { Timer_StartA(); }

/* StartTimers_D @ 0x9e7f4 is the outlier: four ids, and 0xC351 gets an interval of
 * ZERO. Win32 treats a zero interval as "fire as often as possible", not as
 * "disabled", so it is reproduced verbatim rather than corrected. */
void Timer_StartD(void)
{
    Timer_StartOne(PUFO_TIMER_RENDER, 0);
    Timer_StartOne(PUFO_TIMER_ANIM, PUFO_TIMER_MS_D_ANIM);
    Timer_StartOne(PUFO_TIMER_UIPULSE, PUFO_TIMER_MS_D_PULSE);
    Timer_StartOne(PUFO_TIMER_EXTRA, PUFO_TIMER_MS_D_EXTRA);
}

/* KillTimers_D @ 0x9e864 kills the same four ids. */
void Timer_KillD(void)
{
    Timer_KillOne(PUFO_TIMER_RENDER);
    Timer_KillOne(PUFO_TIMER_ANIM);
    Timer_KillOne(PUFO_TIMER_UIPULSE);
    Timer_KillOne(PUFO_TIMER_EXTRA);
}

/* ------------------------------------------------------------ the speed-driven set */

/* KillTimers_BySpeed @ 0x9e8b4 kills 0xC350..0xC353 unconditionally, including
 * 0xC350 which StartTimers_BySpeed may never have set (any speed above 4). */
void Timer_KillBySpeed(void)
{
    Timer_KillOne(PUFO_TIMER_GAMETICK);
    Timer_KillOne(PUFO_TIMER_RENDER);
    Timer_KillOne(PUFO_TIMER_ANIM);
    Timer_KillOne(PUFO_TIMER_UIPULSE);
}

/* StartTimers_BySpeed @ 0x9e904. The switch has no default arm, so a speed of 5
 * or more sets neither game-rate timer -- while still setting the two trailing
 * timers. That asymmetry is the original's and is reproduced exactly. */
void Timer_StartBySpeed(u32 nGameSpeed)
{
    switch (nGameSpeed) {
        case 0:
            Timer_StartOne(PUFO_TIMER_GAMETICK, PUFO_TIMER_MS_SLOW);
            Timer_StartOne(PUFO_TIMER_RENDER, PUFO_TIMER_MS_SLOW);
            break;
        case 1:
            Timer_StartOne(PUFO_TIMER_GAMETICK, PUFO_TIMER_MS_MID);
            Timer_StartOne(PUFO_TIMER_RENDER, PUFO_TIMER_MS_MID);
            break;
        case 2:
        case 3:
        case 4:
            Timer_StartOne(PUFO_TIMER_GAMETICK, PUFO_TIMER_MS_FAST);
            Timer_StartOne(PUFO_TIMER_RENDER, PUFO_TIMER_MS_FAST);
            break;
        default:
            /* No arm: speeds above 4 leave both game-rate timers untouched. */
            break;
    }

    /* Always, for every speed: the animation and UI-pulse timers. */
    Timer_StartOne(PUFO_TIMER_ANIM, PUFO_TIMER_MS_ANIM);
    Timer_StartOne(PUFO_TIMER_UIPULSE, PUFO_TIMER_MS_PULSE);
}

/* ---------------------------------------------------------------- tactctl bridge */

/* GameTimers' callbacks take only a user pointer, but StartTimers_BySpeed needs
 * the current speed. GameSpeed_Set writes the clock and then calls pfnStartAll,
 * so the bridge reads the speed back from that same clock rather than keeping a
 * second copy that could drift. */
static GameClock *s_pClock;

static void Bridge_KillAll(void *pUser)
{
    (void)pUser;
    Timer_KillBySpeed();
}

static void Bridge_StartAll(void *pUser)
{
    (void)pUser;
    Timer_StartBySpeed(s_pClock != NULL ? s_pClock->nSpeed : 0u);
}

void Timer_InstallAsGameTimers(GameClock *pClock)
{
    GameTimers timers;

    s_pClock = pClock;
    timers.pfnKillAll  = Bridge_KillAll;
    timers.pfnStartAll = Bridge_StartAll;
    timers.pUser       = NULL;
    GameSpeed_SetTimers(&timers);
}

/* ------------------------------------------------------- GameTick_OnTimer */

bool GameTick_OnTimer(const GameTickCtx *pCtx, StatusStack *pStack, TactState *pState,
                      const GameTickHooks *pHooks,
                      MapCellLookup pfnLookup, void *pLookupUser)
{
    if (pCtx == NULL) {
        return false;
    }

    /* Gate 1: the hold flag stops the handler entirely. */
    if (pCtx->bGameTickHold) {
        return false;
    }

    /* Gate 2: the geoscape path does nothing. 0x27514 loads the geoscape screen block into R0 and
     * 0x27518 calls Geoscape_GameTickStub @ 0x94550 (nullsub_9 in the IDB until this part), which is
     * four bytes -- `BX LR`, no body -- and then 0x2751C returns. The 0xC350 tick is not the geoscape
     * clock at all: that is 0xC353's UiPulse_OnTimer @ 0x36630. Reported as "ran" because the handler
     * took its own branch rather than being gated out. */
    if (pCtx->bGeoscapeUi) {
        return true;
    }

    /* Gate 3: the UI slot must be either 10 or "none" (which the original models as
     * the integer 0, because it compares a pointer field against 10). */
    if (pCtx->nUiSlot10 != 10 && pCtx->nUiSlot10 != 0) {
        return false;
    }

    /* Gate 4: no tactical map, no tactical tick. */
    if (!pCtx->bTactFlag) {
        return false;
    }

    /* Everything past gate 4 touches the TTL stack (Map+0x54/0x140AC), TactCtx272 and
     * pMapTactical -- the geoscape stub return touches none of them, so a caller without a
     * live tactical frame can still take gates 1 and 2 exactly like the original. Past this
     * point both records are required. */
    if (pStack == NULL || pState == NULL) {
        return false;
    }

    /* The message TTL decay is gated on !busy -- but GameTick_Step below is not. */
    if (!pCtx->bBusy) {
        for (i32 i = 0; i < pStack->nCount; ++i) {
            pStack->anTtl[i] -= 1;
        }
        /* The shift tests only the OLDEST message, after the whole array was
         * decremented, so at most one shift happens per tick. */
        if (pStack->nCount > 0 && pStack->anTtl[0] <= 0) {
            StatusMsg_Shift(pStack);
        }
    }

    /* Always, busy or not. */
    (void)GameTick_Step(pState, pfnLookup, pLookupUser);

    /* TickActiveUnit is skipped only when BOTH conditions hold. */
    if (!(pState->bPhaseActive && pCtx->bSlot8Byte1)) {
        if (pHooks != NULL && pHooks->pfnTickActiveUnit != NULL) {
            pHooks->pfnTickActiveUnit(pHooks->pUser);
        }
    }

    if (pHooks != NULL && pHooks->pfnClearFrameScratch != NULL) {
        pHooks->pfnClearFrameScratch(pHooks->pUser);
    }
    return true;
}

/* ------------------------------------------------------- the 0xC350 dispatch rule */

/* bGameTickExtraDue @ 0x18838C: read at 0xA0914, written at 0xA0920 and 0xA092C, by nothing else. */
static i32 s_bTickExtraDue;

/* PocketUFO_WndProc @ 0xA08C0..0xA0934. The speed is read signed (BMI at 0xA08C8), so a negative
 * value ticks nothing rather than wrapping into the switch. */
void Timer_TickBySpeed(i32 nGameSpeed, GameTickFn pfnTick, void *pUser)
{
    if (nGameSpeed < 0) {
        return; /* 0xA08C8 */
    }

    if (nGameSpeed > 2) {
        if (nGameSpeed == 3) { /* 0xA08D8 */
            pfnTick(pUser);    /* 0xA0904 */
            if (s_bTickExtraDue != 0) {
                pfnTick(pUser);        /* 0xA091C */
                s_bTickExtraDue = 0;   /* 0xA0920 */
            } else {
                s_bTickExtraDue = 1;   /* 0xA092C */
            }
            return;            /* 0xA0934 */
        }
        if (nGameSpeed != 4) {
            return;            /* 0xA08E0: speeds above 4 tick nothing at all */
        }
        pfnTick(pUser);        /* 0xA08E8 */
        pfnTick(pUser);        /* 0xA08F0 */
    }

    pfnTick(pUser);            /* 0xA08F8: the common tick every reachable speed shares */
}
