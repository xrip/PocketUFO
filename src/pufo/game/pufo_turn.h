/* pufo_turn.h - the tactical turn state machine and per-phase cursor handling.
 *
 * Evidence: TactCtx272_ctor @ 0x4c9f8, Tact_ProcessActionPhase @ 0x4d950,
 *           Tact_BeginEnemyTurn @ 0x4ca18, Tact_EndEnemyTurn @ 0x4cb74,
 *           Tact_RefreshTeamVisibility @ 0x4f368.
 *
 * ## TactCtx272
 *
 * 272 bytes, pointer-heavy. The field offsets are proven from
 * `Tact_ProcessActionPhase`, which indexes the opportunity-fire list as
 * `pCtx->apOppFire[i]` via `*(&pCtx->pScanUnit + i)`:
 *
 *     0x000 pScanUnit      0x004 apOppFire[60]      0x0F4 nOppFireCount
 *     0x0F8 bBusy          0x0FC nPhase             0x100 pSavedFocus
 *     0x104 pSavedSelUnit  0x108 pCameraUnit        0x10C pTrackUnit
 *
 * Because it holds pointers the Win64 size differs; only field access matters,
 * and this context is never serialised (it is rebuilt per mission by
 * `Tact_InitMission` @ `0x3c550`).
 *
 * ## The action phase machine
 *
 * `Tact_ProcessActionPhase` is a small state machine over `bBusy` x `nPhase`
 * with these states, all verified from the decompilation:
 *
 *   busy=0, phase=1 : player opportunity fire. Disable input, save the cursor,
 *                     fire at the current opportunity target, and when the list
 *                     is exhausted set phase 2 and raise bUnk70103 on the queue.
 *   busy=0, phase=2 : restore input and the cursor, phase -> 0.
 *   busy=1, phase=1 : same as busy=0 phase=1 but WITHOUT touching input or
 *                     restoring the cursor at the end (phase -> 2).
 *   busy=1, phase=2 : restore the cursor and layer, then phase -> 3.
 *   busy=1, phase=3 : Tact_EnemyAiTick.
 *
 * Every opportunity-fire arm walks `nOppFireCount` down past any entry whose
 * bFlags has bit 0x20 set (a unit that cannot act), and stops when the count
 * reaches 0. It is reproduced exactly, including the `<= 0` exit.
 *
 * The cursor is saved into `anActionPhaseSave0..2` (the context bytes at
 * 0x15078) and restored on the way out, so a fire action can move the camera
 * without disturbing where the player left the pointer.
 */
#ifndef PUFO_TURN_H
#define PUFO_TURN_H

#include "game/pufo_tactctl.h"
#include "game/pufo_tactstate.h"
#include "game/pufo_unit.h"
#include "pufo_common.h"

#define PUFO_OPPFIRE_MAX 60

typedef struct TactCtx272 {
    Soldier *pScanUnit;                       /* 0x000 */
    Soldier *apOppFire[PUFO_OPPFIRE_MAX];     /* 0x004 */
    i32      nOppFireCount;                   /* 0x0F4 */
    u8       bBusy;                           /* 0x0F8 */
    i32      nPhase;                          /* 0x0FC */
    Soldier *pSavedFocus;                     /* 0x100 */
    Soldier *pSavedSelUnit;                   /* 0x104 */
    Soldier *pCameraUnit;                     /* 0x108 */
    Soldier *pTrackUnit;                      /* 0x10C */
} TactCtx272;

/* The cursor snapshot the phase machine saves and restores. Mirrors
 * anActionPhaseSave0/1/2 @ 0x15078. */
typedef struct ActionPhaseSave {
    i32 nCursorX;
    i32 nCursorY;
    i32 nLayer;
} ActionPhaseSave;

/* What the phase machine needs from the game. Injected so the state transitions
 * are testable without a map, a renderer, or the AI.
 *
 * `pfnSelectUnit` and `pfnFocusSelected` exist because the original calls
 * Tact_SelectUnit and MapView_FocusSelected; `pfnRequestFire` stands in for
 * TactState_EnqueueFireBurst @ 0x4b080 plus the following KickQueue @ 0x4f334,
 * which it must perform together. `pfnEnemyAiTick` stands in for
 * Tact_EnemyAiTick @ 0x4cbe0. */
typedef struct TurnHooks {
    void (*pfnSetPlayerInput)(bool bEnabled, void *pUser);
    void (*pfnSetCursor)(i32 nX, i32 nY, i32 nLayer, void *pUser);
    void (*pfnReadCursor)(i32 *pnX, i32 *pnY, i32 *pnLayer, void *pUser);
    void (*pfnSelectUnit)(Soldier *pUnit, void *pUser);
    void (*pfnFocusSelected)(void *pUser);
    void (*pfnRequestFire)(Soldier *pUnit, i32 nX, i32 nY, i32 nZ, void *pUser);
    void (*pfnEnemyAiTick)(TactCtx272 *pCtx, void *pUser);
    void (*pfnEnqueueEndOfOppFire)(void *pUser); /* sets bUnk70103 on the queue */
    void *pUser;
} TurnHooks;

/* TactCtx272_ctor @ 0x4c9f8: zeroes the context. */
void TactCtx_ctor(TactCtx272 *pCtx);

void Tact_ProcessActionPhase(TactCtx272 *pCtx, const TurnHooks *pHooks,
                             ActionPhaseSave *pSave);

/* Tact_BeginEnemyTurn @ 0x4ca18.
 *
 * Sets bEnemyTurnActive, marks the context busy, clears every side's per-unit AI
 * and spotting state, and starts the action phase at phase 3. The original also
 * calls Render_Frame @ 0x22310 and BeforeEnemyStep @ 0x4b9b8 first; those are
 * injected via `pfnFrameStart` and `pfnBeforeEnemyStep` because both need the
 * full world (BeforeEnemyStep is 2792 bytes).
 *
 * `apUnits` plus `nUnits` describe the unit lists to reset. The original resets
 * three lists in this order: enemy, civilian, then XCom. */
typedef struct TurnUnitLists {
    Soldier **apEnemy;
    i32       nEnemy;
    Soldier **apCivilian;
    i32       nCivilian;
    Soldier **apXcom;
    i32       nXcom;
} TurnUnitLists;

void Tact_BeginEnemyTurn(TactCtx272 *pCtx, const TurnHooks *pHooks,
                         const TurnUnitLists *pLists,
                         void (*pfnFrameStart)(void *pUser),
                         void (*pfnBeforeEnemyStep)(TactCtx272 *pCtx, void *pUser));

/* Tact_EndEnemyTurn @ 0x4cb74.
 *
 * Clears bEnemyTurnActive and busy, re-enables input, restores the selected unit
 * (falling back to the first XCom unit when the saved one is dead or flagged
 * 0x20), selects it, and re-focuses the view. `pfnAfterEnemyStep` stands in for
 * AfterEnemyStep @ 0x4c4a0. The first XCom unit is passed in because the port
 * does not yet own the side lists. */
void Tact_EndEnemyTurn(TactCtx272 *pCtx, const TurnHooks *pHooks, Soldier *pFirstXcom,
                       void (*pfnAfterEnemyStep)(TactCtx272 *pCtx, void *pUser));

/* Tact_RefreshTeamVisibility @ 0x4f368.
 *
 * Marks the queue entry at the current cursor as command 12 (the same END_MARK
 * TactState_EnqueueEndMark emits), sets a queue length of nQueueCursor + 2, raises
 * bFlag1, and refreshes visibility for every XCom unit.
 *
 * The first three writes address the queue by raw offset in the original, so they
 * are expressed here through the queue's own fields:
 *     aQueue[nQueueCursor].nCmd = TACTCMD_END_MARK;
 *     nQueueLen = nQueueCursor + 2;
 *     bFlag1 = 1;
 *
 * `pfnUpdateVisibility` stands in for Unit_UpdateVisibility @ 0x48580 (3728
 * bytes, its own body of work). */
typedef void (*TurnUpdateVisibilityFn)(Soldier *pUnit, void *pUser);

void Tact_RefreshTeamVisibility(TactState *pQueue, Soldier **apXcom, i32 nXcom,
                                TurnUpdateVisibilityFn pfnUpdateVisibility,
                                void *pUser);

/* Unit_bScanOpportunityFire @ 0x48028.
 *
 * Resets the context's opportunity-fire list (`nOppFireCount = 0`, `pScanUnit =
 * pUnit`) and, when the unit is operative, walks the enemy roster; otherwise it
 * walks the XCom roster. Every shooter whose morale is strictly above 0x1E is
 * offered through `pfnTryOpportunityFire` (the port of Unit_bTryOpportunityFire
 * @ 0x47c38). The loop re-reads the side's count after each offer, exactly as the
 * original does when a target may have died. Returns whether any opportunity fire
 * was queued. The two counts are passed by pointer so the re-read is preserved. */
bool Unit_bScanOpportunityFire(TactCtx272 *pCtx, Soldier *pUnit,
                               Soldier *const *apEnemy, const i32 *pnEnemyCount,
                               Soldier *const *apXcom, const i32 *pnXcomCount,
                               void (*pfnTryOpportunityFire)(Soldier *pTarget,
                                                             Soldier *pShooter,
                                                             void *pUser),
                               void *pUser);

#endif /* PUFO_TURN_H */
