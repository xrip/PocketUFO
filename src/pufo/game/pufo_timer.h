/* pufo_timer.h - the game's Win32 timer set.
 *
 * Evidence: KillTimers_A @ 0x9e6a4, StartTimers_A @ 0x9e6d4, StartTimers_B
 *           @ 0x9e714, KillTimers_B @ 0x9e754, StartTimers_C @ 0x9e784,
 *           KillTimers_C @ 0x9e7c4, StartTimers_D @ 0x9e7f4, KillTimers_D
 *           @ 0x9e864, KillTimers_BySpeed @ 0x9e8b4, StartTimers_BySpeed
 *           @ 0x9e904.
 *
 * ## Timer ids
 *
 * Every call uses the global `hWndMain` @ 0x188d54 and one of five ids:
 *
 *     0xC350  GameTick      interval set by nGameSpeed (100 / 50 / 10 ms)
 *     0xC351  Render_Frame  same interval as GameTick
 *     0xC352  animation     25 ms
 *     0xC353  UI pulse      50 ms
 *     0xC354  (only StartTimers_D starts it, at 10 ms)
 *
 * ## The four plain sets
 *
 * `KillTimers_A/B/C` and `StartTimers_A/B/C` are **three byte-identical pairs**:
 *
 *     Kill  A/B/C : kill 0xC351, 0xC352
 *     Start A/B/C : 0xC351 = 40 ms, 0xC352 = 25 ms
 *
 * That is an original redundancy, not a transcription error, and the port keeps
 * the three names so the call sites stay recognisable.
 *
 * `StartTimers_D` is the only one that touches four ids:
 *
 *     0xC351 = 0, 0xC352 = 50 ms, 0xC353 = 20 ms, 0xC354 = 10 ms
 *
 * Note the zero interval for 0xC351, which Win32 treats as "as fast as possible"
 * rather than "off". `KillTimers_D` kills that same four-id set.
 *
 * ## The speed-driven set
 *
 * `StartTimers_BySpeed` is what `GameSpeed_Set` @ 0x260c0 calls. It switches on
 * `nGameSpeed` and sets 0xC350 with a matching 0xC351:
 *
 *     speed 0     : 0xC350 = 100 ms, 0xC351 = 100 ms
 *     speed 1     : 0xC350 =  50 ms, 0xC351 =  50 ms
 *     speed 2,3,4 : 0xC350 =  10 ms, 0xC351 =  10 ms
 *     speed 5+    : neither is set (the switch has no default arm)
 *
 * Then, for every speed including 5+, it sets 0xC352 = 25 ms and 0xC353 = 50 ms.
 * So an out-of-range speed leaves the two game-rate timers alone while still
 * restarting the animation and UI timers.
 *
 * `KillTimers_BySpeed` unconditionally kills 0xC350..0xC353, again including
 * 0xC350 which the speed-driven start may never have set.
 */
#ifndef PUFO_TIMER_H
#define PUFO_TIMER_H

#include "game/pufo_statusmsg.h"
#include "game/pufo_tactctl.h"
#include "game/pufo_tactstate.h"
#include "pufo_common.h"

#include <windows.h>

/* Timer ids, verbatim from the decompilation. */
#define PUFO_TIMER_GAMETICK 0xC350u
#define PUFO_TIMER_RENDER   0xC351u
#define PUFO_TIMER_ANIM     0xC352u
#define PUFO_TIMER_UIPULSE  0xC353u
#define PUFO_TIMER_EXTRA    0xC354u

/* Intervals in milliseconds. */
#define PUFO_TIMER_MS_FAST   10u  /* speeds 2..4 */
#define PUFO_TIMER_MS_MID    50u  /* speed 1 */
#define PUFO_TIMER_MS_SLOW  100u  /* speed 0 */
#define PUFO_TIMER_MS_ANIM   25u  /* 0xC352 in the plain sets */
#define PUFO_TIMER_MS_PULSE  50u  /* 0xC353 */
#define PUFO_TIMER_MS_RENDER 40u  /* 0xC351 in the plain sets */
/* StartTimers_D's own intervals. */
#define PUFO_TIMER_MS_D_ANIM   50u
#define PUFO_TIMER_MS_D_PULSE  20u
#define PUFO_TIMER_MS_D_EXTRA  10u

/* Sets the window the timers attach to. The original reads the global hWndMain
 * @ 0x188d54; the port takes it explicitly so the module has no hidden state and
 * a test can drive it. Returns false when hWnd is NULL. */
bool Timer_SetWindow(HWND hWnd);

/* The Win32 calls the module makes. Injected so the id/interval table above can be
 * verified without a window, and because Win32 timers are the only untestable
 * part of this layer. Defaults to the real SetTimer/KillTimer. */
typedef struct TimerBackend {
    void (*pfnSet)(HWND hWnd, u32 nId, u32 nMs, void *pUser);
    void (*pfnKill)(HWND hWnd, u32 nId, void *pUser);
    void *pUser;
} TimerBackend;

void Timer_SetBackend(const TimerBackend *pBackend);

/* The four plain pairs. */
void Timer_KillA(void);
void Timer_StartA(void);
void Timer_KillB(void);
void Timer_StartB(void);
void Timer_KillC(void);
void Timer_StartC(void);
void Timer_KillD(void);
void Timer_StartD(void);

/* The speed-driven pair, the ones GameSpeed_Set calls. */
void Timer_KillBySpeed(void);
void Timer_StartBySpeed(u32 nGameSpeed);

/* Installs the speed-driven pair into pufo_tactctl's injected backend, so
 * GameSpeed_Set @ 0x260c0 drives the real timer set.
 *
 * `pClock` is the clock GameSpeed_Set writes, needed because GameTimers'
 * callbacks carry no speed argument; pass NULL to install a version that reads a
 * speed recorded through Timer_SetPendingSpeed instead. */
void Timer_InstallAsGameTimers(GameClock *pClock);

/* ------------------------------------------------------------------ the tick handler
 *
 * GameTick_OnTimer @ 0x274f0 is the 0xC350 WM_TIMER handler. It is the one place
 * the per-frame decay, the tactical tick and the active-unit tick are driven from,
 * so it is the join between the timer set, the status stack and the action queue.
 *
 * Verified control flow:
 *
 *     if (bGameTickHold) return;                     // 0x5A, the pause flag
 *     if (bGeoscapeUi)   { return; }                 // 0x44: R0 = the geoscape screen block,
 *                                                    // BL Geoscape_GameTickStub @ 0x94550, which is
 *                                                    // 4 bytes -- `BX LR`, no body. The 0xC350 tick
 *                                                    // does nothing in the geoscape; that clock is
 *                                                    // 0xC353's (UiPulse_OnTimer @ 0x36630).
 *     if (uiSlot10 != 10 && uiSlot10 != NULL) return; // apUiSlotsA[10] @ 0x88
 *     if (!bTactFlag)     return;                    // 0x00
 *
 *     if (!busy) {
 *         for (i = 0; i < nStatusMsgCount; ++i) --anStatusMsgTTL[i];
 *         if (anStatusMsgTTL[0] <= 0) StatusMsg_Shift();
 *     }
 *     GameTick_Step();
 *     if (!phaseActive || slot8Byte1 == 0) TickActiveUnit();
 *     frameScratch = 0;                              // byte_138E74
 *
 * Three details that are easy to get wrong:
 *
 *   1. The message TTL decay is gated on `!busy`, but `GameTick_Step` is NOT -- the
 *      grid decay and the phase counter keep running while the game is busy.
 *   2. The shift tests `anStatusMsgTTL[0] <= 0` AFTER the whole array has been
 *      decremented, not per message, so a stack where only the oldest expires shifts
 *      exactly once per tick.
 *   3. `TickActiveUnit` is SKIPPED when the phase is active AND slot8's second byte
 *      is set. Either condition alone still ticks the unit.
 *
 * `uiSlot10` is a pointer field compared against the integer 10 in the original,
 * which is why the port models it as an integer value rather than a pointer.
 */
typedef struct GameTickCtx {
    bool bGameTickHold;   /* 0x5A */
    bool bGeoscapeUi;     /* 0x44 */
    bool bTactFlag;       /* 0x00 */
    i32  nUiSlot10;       /* apUiSlotsA[10] @ 0x88 */
    bool bBusy;           /* TactCtx272.bBusy */
    bool bPhaseActive;    /* TactState.bUnk70115 */
    /* Part 105: the slot-8 byte is a POINTER because the 0x275CC skip gate runs AFTER GameTick_Step
     * (0x275b0 is the step call inside this handler) and GameTick_Step's own wrap CLEARS the byte at
     * 0x274B4. A value copy captured at entry would let a step that just ended its phase keep
     * TickActiveUnit skipped, where the original -- reading Map+0x81 live at 0x275CC -- ticks. The
     * pair cell goes through too: GameTick_Step @ 0x27324 reads/writes both (0x27364/0x27388,
     * 0x274B4/0x274BC). NULL means "no such cell" and reads as zero. */
    u8   *pbSlot8Byte1;     /* unk_113A39 @ Map+0x81 = BYTE1(apUiSlotsA[8]) */
    u8   *pbEnemyTurnActive;/* Map_bEnemyTurnActive @ Map+0x5B */
} GameTickCtx;

typedef struct GameTickHooks {
    void (*pfnTickActiveUnit)(void *pUser);    /* Tact_TickActiveUnit @ 0x54130, ported in
                                                * pufo_tactstate.c; a hook because it takes the caller's
                                                * TactTickCtx, not this handler's arguments */
    void (*pfnClearFrameScratch)(void *pUser); /* byte_138E74 = 0 */
    void (*pfnRenderFrame)(void *pUser);       /* GameTick_Step's phase-wrap tail call to Render_Frame
                                                * @ 0x274C4 (Render_Frame @ 0x22310 over the pIso global
                                                * block); forwarded to GameTickStepCells.pfnRenderFrame */
    void *pUser;
} GameTickHooks;

/* Returns true when the handler did any work (i.e. it did not return early). */
bool GameTick_OnTimer(const GameTickCtx *pCtx, StatusStack *pStack, TactState *pState,
                      const GameTickHooks *pHooks,
                      MapCellLookup pfnLookup, void *pLookupUser);

/* ---------------------------------------------------------- the 0xC350 dispatch rule
 *
 * PocketUFO_WndProc's WM_TIMER arm for 0xC350 (0xA08B0 gate, 0xA08C0..0xA0934) is not "tick once":
 * the game speed decides how many ticks one firing of the timer runs.
 *
 *     nGameSpeed < 0   : 0 ticks (BMI 0xA08C8)
 *     0, 1, 2          : 1 tick (BLE 0xA08D0 -> 0xA08F4 / 0xA08F8)
 *     3                : 1 tick at 0xA0904 plus a second on ALTERNATE firings -- 0xA0908 reads
 *                        bGameTickExtraDue @ 0x18838C, 0xA091C runs the extra and 0xA0920 clears the
 *                        flag, 0xA092C sets it when the extra was skipped. The flag starts 0, so the
 *                        firings run 1, 2, 1, 2 ... and the average rate is 1.5 ticks.
 *     4                : 3 ticks (0xA08E8, 0xA08F0, then the common 0xA08F8)
 *     nGameSpeed > 4   : 0 ticks (BNE 0xA08E0)
 *
 * `pfnTick` is the caller's GameTick_OnTimer call, because that handler needs the tactical state and
 * the status stack, which belong to whoever owns them. The alternation flag lives here because the
 * original keeps it in .bss (0x18838C, just past the 12-byte SHACTIVATEINFO at 0x188380) where nothing
 * else touches it: read only at 0xA0914, written only at 0xA0920 and 0xA092C.
 */
typedef void (*GameTickFn)(void *pUser);
void Timer_TickBySpeed(i32 nGameSpeed, GameTickFn pfnTick, void *pUser);

#endif /* PUFO_TIMER_H */
