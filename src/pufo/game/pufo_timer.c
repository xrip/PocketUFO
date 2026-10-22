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

/* KillTimers_A @ 0x9e6a4 (48 bytes): KillTimer(hWndMain, 0xC351) at 0x9E6B8, then POP {R4,LR} at
 * 0x9E6C8 and the tail `B KillTimer` with 0xC352 at 0x9E6CC. Those are the only two ids: 0xC350 and
 * 0xC353 keep running through this call. */
void Timer_KillA(void)
{
    Timer_KillOne(PUFO_TIMER_RENDER);
    Timer_KillOne(PUFO_TIMER_ANIM);
}

/* KillTimers_B @ 0x9e754: the same two kills, at 0x9E768 and the tail 0x9E77C. */
void Timer_KillB(void) { Timer_KillA(); }

/* KillTimers_C @ 0x9e7c4: the same two kills, at 0x9E7D8 and the tail 0x9E7EC. Reached from
 * Titles_Shutdown (0x38274). */
void Timer_KillC(void) { Timer_KillA(); }

/* StartTimers_A @ 0x9e6d4 (64 bytes): SetTimer with id 0xC300|0x51 and uTimeOut 0x28 (40 ms) at
 * 0x9E6F0, then POP {R4,LR} at 0x9E708 and the tail `B SetTimer` with 0xC300|0x52 and 0x19 (25 ms)
 * at 0x9E70C. pfnTimerProc is 0 in both calls, so every timer posts WM_TIMER to the window. */
void Timer_StartA(void)
{
    Timer_StartOne(PUFO_TIMER_RENDER, PUFO_TIMER_MS_RENDER);
    Timer_StartOne(PUFO_TIMER_ANIM, PUFO_TIMER_MS_ANIM);
}

/* StartTimers_B @ 0x9e714: the same pair, at 0x9E730 and the tail 0x9E74C. Reached from
 * GameUI_InitStartup (0x38D64). */
void Timer_StartB(void) { Timer_StartA(); }

/* StartTimers_C @ 0x9e784: the same pair, at 0x9E7A0 and the tail 0x9E7BC. */
void Timer_StartC(void) { Timer_StartA(); }

/* The three Start copies and the three Kill copies are identical but for their branch words: a byte
 * compare of 0x9e6d4/0x9e714/0x9e784 differs only at +0x1C and +0x38 (0x9E6F0 and 0x9E70C, the two
 * SetTimer branches), and 0x9e6a4/0x9e754/0x9e7c4 only at +0x14 and +0x28 (0x9E6B8 and 0x9E6CC).
 * Those four words are the branch displacements to the shared SetTimer/KillTimer import thunk, so
 * the six bodies carry the same ids and the same intervals. */

/* StartTimers_D @ 0x9e7f4 (112 bytes) is the outlier: four ids, and 0xC351 gets an interval of
 * ZERO -- MOV R2,#0 at 0x9E808, SetTimer at 0x9E810. Win32 treats a zero interval as "fire as often
 * as possible", not as "disabled", so it is reproduced verbatim rather than corrected. Then
 * 0xC352 = 0x32 (50 ms) at 0x9E828, 0xC353 = 0x14 (20 ms) at 0x9E840, and the tail call
 * POP {R4,LR} at 0x9E858 with 0xC354 = 0xA (10 ms) at 0x9E85C. Note the intervals here are not the
 * plain set's: the pulse is 20 ms not 50, and 0xC354 exists only in this function. */
void Timer_StartD(void)
{
    Timer_StartOne(PUFO_TIMER_RENDER, 0);
    Timer_StartOne(PUFO_TIMER_ANIM, PUFO_TIMER_MS_D_ANIM);
    Timer_StartOne(PUFO_TIMER_UIPULSE, PUFO_TIMER_MS_D_PULSE);
    Timer_StartOne(PUFO_TIMER_EXTRA, PUFO_TIMER_MS_D_EXTRA);
}

/* KillTimers_D @ 0x9e864 (80 bytes): KillTimer on 0xC351 at 0x9E878, 0xC352 at 0x9E888, 0xC353 at
 * 0x9E898, and the tail `B KillTimer` on 0xC354 at 0x9E8AC. It kills exactly what StartTimers_D
 * started and nothing else -- 0xC350 keeps running across this call. */
void Timer_KillD(void)
{
    Timer_KillOne(PUFO_TIMER_RENDER);
    Timer_KillOne(PUFO_TIMER_ANIM);
    Timer_KillOne(PUFO_TIMER_UIPULSE);
    Timer_KillOne(PUFO_TIMER_EXTRA);
}

/* ------------------------------------------------------------ the speed-driven set */

/* KillTimers_BySpeed @ 0x9e8b4 (80 bytes): KillTimer on 0xC350 at 0x9E8C8, 0xC351 at 0x9E8D8,
 * 0xC352 at 0x9E8E8, tail `B KillTimer` on 0xC353 at 0x9E8FC. Unconditional, including 0xC350 which
 * StartTimers_BySpeed may never have set (any speed outside 0..4). 0xC354 is not in this list --
 * only KillTimers_D touches that id. */
void Timer_KillBySpeed(void)
{
    Timer_KillOne(PUFO_TIMER_GAMETICK);
    Timer_KillOne(PUFO_TIMER_RENDER);
    Timer_KillOne(PUFO_TIMER_ANIM);
    Timer_KillOne(PUFO_TIMER_UIPULSE);
}

/* StartTimers_BySpeed @ 0x9e904 (236 bytes). The speed is read from the live Map block at 0x9E918
 * (`MOV R12,#(Map_nGameSpeed-0x1139B8)` at 0x9E910) and compared with an UNSIGNED chain: CMP/BEQ for
 * 0 at 0x9E91C/0x9E920, for 1 at 0x9E924/0x9E928, for 2 and 3 at 0x9E92C..0x9E938, then CMP #4 at
 * 0x9E93C with `BNE 0x9E9BC` at 0x9E940 -- so speed 4 reaches the 2/3 block by falling through, and
 * any other value (5, or a negative stored speed, which reads as a huge unsigned) jumps straight to
 * the tail block and sets neither game-rate timer. The blocks: 0x9E98C for speed 0 (SetTimer 0xC350
 * with 0x64 at 0x9E9A0, then MOV R2,#0x64 at 0x9E9A4 which FALLS THROUGH), 0x9E96C for speed 1
 * (0x32 at 0x9E980, `B 0x9E9A8`), 0x9E944 for speeds 2/3/4 (0xA at 0x9E958, `B 0x9E9A8`). They join
 * at 0x9E9A8, where SetTimer takes id 0xC300|0x51 with the speed interval (call at 0x9E9B8), and
 * that block falls through into 0x9E9BC: 0xC352 = 0x19 (25 ms) at 0x9E9D0, then POP {R4,LR} at
 * 0x9E9E8 and the tail `B SetTimer` with 0xC353 = 0x32 (50 ms) at 0x9E9EC. So the two trailing
 * timers are set for every speed including the out-of-range one, while 0xC350/0xC351 belong only to
 * 0..4. That asymmetry is the original's and is reproduced exactly. */
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

    /* Always, busy or not. The step owns the slot-8 byte pair (0x27364/0x27388 set,
     * 0x274B4/0x274BC clear) and the wrap's tail Render_Frame (0x274C4). */
    {
        GameTickStepCells cells;
        cells.pbSlot8Byte1      = pCtx->pbSlot8Byte1;
        cells.pbEnemyTurnActive = pCtx->pbEnemyTurnActive;
        cells.pfnRenderFrame    = (pHooks != NULL) ? pHooks->pfnRenderFrame : NULL;
        cells.pRenderUser       = (pHooks != NULL) ? pHooks->pUser : NULL;
        (void)GameTick_Step(pState, &cells, pfnLookup, pLookupUser);
    }

    /* TickActiveUnit is skipped only when BOTH conditions hold -- and the original reads the
     * slot-8 byte LIVE at 0x275CC, after the 0x275b0 call to GameTick_Step. A wrap step clears
     * the byte at 0x274B4, so the step's own tick is the last one that skips the walker. */
    if (!(pState->bPhaseActive && pCtx->pbSlot8Byte1 != NULL && *pCtx->pbSlot8Byte1 != 0)) {
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
