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
 *     0x0F8 bBusy          0x0FC nPhase             0x100 bAiFocusSaved
 *     0x104 pSavedSelUnit  0x108 pCameraUnit        0x10C pTrackUnit
 *
 * +0x100 is a BYTE flag, not a pointer. It used to be typed `Soldier *pSavedFocus`
 * because Hex-Rays reads it as `LOBYTE(pCtx->pSavedFocus)`, but every access in the
 * binary is a 0/1 byte store or test (STRB at 0x4CC00, 0x4D688, 0x4D84C, 0x4D86C,
 * 0x4D878, 0x4D888 and the test at 0x4D920) and nothing ever loads it as an address.
 * The unit the enemy turn restores lives at +0x104, which is what Tact_EndEnemyTurn
 * @ 0x4cb74 actually reads.
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

#include "game/pufo_rmp.h"      /* RmpData, for the hidden-reserve spawn */
#include "game/pufo_tactctl.h"
#include "game/pufo_tactstate.h"
#include "game/pufo_unit.h"
#include "pufo_common.h"

#define PUFO_OPPFIRE_MAX 60

/* The action-phase states, from Tact_ProcessActionPhase @ 0x4d950. Phase 3 is the enemy
 * AI: Tact_EnemyAiTick @ 0x4cbe0 writes it back at its own tail (0x4D920) after every
 * pass, so an AI pass that queued work leaves the machine ready for the next pass. */
#define PUFO_PHASE_NONE          0
#define PUFO_PHASE_OPP_FIRE      1
#define PUFO_PHASE_RESTORE       2
#define PUFO_PHASE_ENEMY_AI      3

typedef struct TactCtx272 {
    Soldier *pScanUnit;                       /* 0x000 */
    Soldier *apOppFire[PUFO_OPPFIRE_MAX];     /* 0x004 */
    i32      nOppFireCount;                   /* 0x0F4 */
    u8       bBusy;                           /* 0x0F8 */
    i32      nPhase;                          /* 0x0FC */
    u8       bAiFocusSaved;                   /* 0x100, set by Tact_EnemyAiTick when this
                                               * tick left the player's focus on a unit; the
                                               * tail at 0x4D920 ends the enemy turn only
                                               * when it is still 0. */
    u8       gap_101[3];                      /* 0x101 */
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
 * Tact_EnemyAiTick (0x4cbe0), NOT ported. */
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

/* Tact_BeginEnemyTurn @ 0x4ca18. The only original caller is MenuBtn_EndTurn @ 0x16494.
 *
 * Clear the camera unit, set bEnemyTurnActive (0x4CA30, `pbEnemyTurnActive`), mark the
 * context busy, run Render_Frame (0x22310, `pfnFrameStart`) and BeforeEnemyStep (0x4b9b8,
 * `pfnBeforeEnemyStep`), set phase 3, then disable player input (0x4CA58 -- after the
 * phase write, like the original), clear every side's per-unit AI and spotting state,
 * and save `pSelUnit` as the context's saved selection (0x4CB4C: the CURRENT
 * Map_pSelUnit, not a roster head -- the head substitution is the EndEnemyTurn tail's)
 * before running the action phase. The two injections exist because both callees need
 * the whole world.
 *
 * `pLists` describes the unit lists to reset. `pnLiveEnemyCount` and
 * `pLists->pnXcomCount` are required live counts: both can change in
 * BeforeEnemyStep, and the original reloads each count after that callback and
 * on every loop pass. The original resets three lists in this order: enemy,
 * civilian, then XCom. */
/* The focused RED/GREEN test can also compile against the frozen preimage header. */
#define PUFO_TURN_HAS_LIVE_XCOM_COUNT 1
typedef struct TurnUnitLists {
    Soldier **apEnemy;
    i32       nEnemy;
    Soldier **apCivilian;
    i32       nCivilian;
    Soldier **apXcom;
    const i32 *pnXcomCount;
} TurnUnitLists;

void Tact_BeginEnemyTurn(TactCtx272 *pCtx, const TurnHooks *pHooks,
                         const TurnUnitLists *pLists, const i32 *pnLiveEnemyCount,
                         Soldier *pSelUnit, u8 *pbEnemyTurnActive,
                         void (*pfnFrameStart)(void *pUser),
                         void (*pfnBeforeEnemyStep)(TactCtx272 *pCtx, void *pUser));

/* Tact_EndEnemyTurn @ 0x4cb74. The only original caller is the Tact_EnemyAiTick tail
 * (0x4D934).
 *
 * Runs AfterEnemyStep (0x4c4a0, `pfnAfterEnemyStep`), clears bEnemyTurnActive (0x4CB88,
 * `pbEnemyTurnActive`) and busy, clears the phase, re-enables input, restores the saved
 * selection (falling back to the first XCom unit when the saved one is dead or flagged
 * 0x20), selects it, and re-focuses the view. The first XCom unit is passed in because
 * the port does not yet own the side lists. */
void Tact_EndEnemyTurn(TactCtx272 *pCtx, const TurnHooks *pHooks, Soldier *pFirstXcom,
                       u8 *pbEnemyTurnActive,
                       void (*pfnAfterEnemyStep)(TactCtx272 *pCtx, void *pUser));

/* Tact_RefreshTeamVisibility @ 0x4f368.
 *
 * Marks the queue entry AFTER the current cursor as command 12 (the same END_MARK
 * TactState_EnqueueEndMark emits), sets a queue length of nQueueCursor + 2, raises
 * bFlag1, and refreshes visibility for every XCom unit.
 *
 * The first three writes address the queue by raw offset in the original: 0x4F380
 * LDR R3,[R0,#0x10] / 0x4F384 MUL R2,R3,#0x38 / 0x4F388 STRB #12,[R0,#0x50], and
 * 0x50 is the aQueue base 0x18 plus one whole 56-byte entry, so the mark lands on
 * aQueue[nQueueCursor + 1]. Expressed through the queue's own fields:
 *     aQueue[nQueueCursor + 1].nCmd = TACTCMD_END_MARK;
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
                               Soldier *const *apEnemy, const u8 *pnEnemyCount,
                               Soldier *const *apXcom, const u8 *pnXcomCount,
                               void (*pfnTryOpportunityFire)(Soldier *pTarget,
                                                             Soldier *pShooter,
                                                             void *pUser),
                               void *pUser);

/* --- the enemy-turn bracket -------------------------------------------------
 *
 * Tact_UpdateUnitControl @ 0x4a444 (788 B), BeforeEnemyStep @ 0x4b9b8 (2792 B) and
 * AfterEnemyStep @ 0x4c4a0 (1368 B). `Tact_BeginEnemyTurn` calls BeforeEnemyStep and
 * `Tact_EndEnemyTurn` calls AfterEnemyStep, so the three bracket `Tact_EnemyAiTick`
 * @ 0x4cbe0; `Tact_UpdateUnitControl` is called by both of them, once per unit.
 *
 * ## Tact_UpdateUnitControl (0x4a444) -- the per-unit side-list transfer
 *
 * Idempotence guard first: `Soldier.bControlRecovered` (+0x1B2) non-zero returns at once,
 * and the function sets it, so each unit is processed once per turn. BeforeEnemyStep clears
 * the flag for the enemy roster (0x4BB80) and AfterEnemyStep for the civilians (0x4C8B0) and
 * the XCom roster (0x4C720) -- which is why the enemy pass runs before the XCom one.
 *
 * The unit arithmetic is `Unit_ApplyControlRecovery` (HP, bUnk1037, uTuSpent, morale) and is
 * reused from there. The original applies morale last, at 0x4A714; the port applies it with
 * the rest because nothing between the two reads bMorale -- TactState_EnqueueRecover,
 * SideLists_RemoveUnit, Tact_SelectUnit and Unit_EnqueueIfApSpent all ignore it.
 *
 * Then the recovery arm (0x4A4E0..0x4A5A4), all six conditions ANDed:
 *     uTuSpent < uHp (0x4A4F0, BCS skips), bFlags bit 0x20 SET (0x4A4FC, BEQ skips), the cell
 *     has no occupant (0x4A50C), uHp != 0 (0x4A51C), !bIsLarge (0x4A52C)
 * -> TactState_EnqueueRecover; and for nUnitType <= 3 the unit's name plus aTextRecs record
 * 39 (male) or 40 (female) is pushed as a FORCED status message; then KickQueue and
 * pLinked1040 = 0.
 *
 * Note the gate polarity: the recovery arm needs bit 0x20 SET (0x4A500) while both side
 * transfers below need it CLEAR (0x4A5C8, 0x4A66C). One unit cannot take both arms in one
 * call. The original is not consistent about the flag and the port reproduces it as-is.
 *
 * Then the two side transfers, both gated on bFlags bit 0x20 being clear:
 *     bStatus == 0 && nUnitType > 8  -> the unit becomes an enemy: remove from the side
 *       lists, bStatus = 1, uTu = 0, append to apEnemyUnits, and if it was Map.pSelUnit
 *       select apXcomUnits[0] instead.
 *     bStatus != 0 && nUnitType <= 8 -> the unit returns to XCom: remove from the side
 *       lists, bStatus = 0, uTu = 0, append to apXcomUnits, and push name + aTextRecs 52
 *       (male) / 53 (female) unprompted.
 * Tail: Unit_EnqueueIfApSpent(pUnit, 0).
 *
 * Both transfers append with the CURRENT count and neither bounds it, so a roster that is
 * already full is written past its end. Recorded in the bug ledger.
 *
 * ## BeforeEnemyStep (0x4b9b8) -- the pre-enemy world tick
 *
 * 1. `pTact->pTrackUnit = NULL`, then Soldier.bUnk1037 = 0 for every XCom unit.
 * 2. XCom-base defence missions only (`Map_nMissionType == MissionType_XcomBase`): spawn
 *    `rand(4) + 2` hidden soldiers, clamped to `Map_nEnemyReserveCount`, each picked with
 *    `rand(nReserveCount)` and removed by swapping the last remaining entry into its slot
 *    (0x4BA98..0x4BB48), then placed with Rmp_PickSpawnForUnit.
 * 3. Every enemy: bControlRecovered = 0, Tact_UpdateUnitControl, then nAiTick = 0.
 * 4. The fire tick, over every cell of all four layers -- see Turn_TickCell below.
 * 5. bControlRecovered = 0 for the civilians, then for the XCom roster.
 * 6. Every enemy refills: uTu = auStat[0], uStunOrArmor = auStat[9].
 * 7. MapCell.bFlags44 bit 0 cleared on every cell of all four layers.
 * 8. UnitList_ForEachRefresh.
 *
 * ### The fire tick
 *
 * A cell burns when `bFlags29 & PUFO_CELL_FLAG_BURNING`. Bit 1 of bFlags29 picks the burn
 * axis: set walks WEST (dir 6) and checks the NORTH side (dir 0) through wall B, clear walks
 * NORTH (dir 0) and checks the WEST side (dir 6) through wall A.
 *
 * The walk starts by finding the run's far end. The ARM multiplies the axis delta by a counter
 * that runs 0, -1, -2 (0x4BC78 `MUL R2, R3, R8` with R8 decremented at 0x4BCF0), so with the
 * west axis the probe steps EAST: offsets 0, +1, +2. Each step requires the cell to be in
 * bounds and its side wall to exist with `Mc1Rec.bBlocksMove` (+0x2A) non-zero. The forward
 * pass then starts at `cell + axisDelta * (nBack + 1)` -- the far end the probe reached -- and
 * walks back along the axis for at most 7 cells, each requiring both the cell
 * and the cell one step in the side direction to be in bounds, the side wall to exist and
 * block, and -- when the cell has an occupant -- that occupant to differ from the occupant
 * of the side cell. If the side cell holds the SAME occupant pointer the run stops there.
 * When the run stopped for any other reason, bFlags29's burning bit is cleared on the first
 * `nRun` cells of it.
 *
 * Every Map_IsValidCell call in the walk passes layer 0 while the cell lookup uses the
 * burning cell's own nZ. Harmless -- Map_IsValidCell only bounds-checks -- but recorded.
 *
 * An occupant of a burning cell is burned: `20 - (1 - abReady252[2]/100) * (bUnk248/3)` is
 * the base, the roll is `rand(base / 4 toward zero)`, and the damage is `base - roll`
 * floored at 0. It comes off uHp (dropping to 0 rather than going negative) and is
 * accumulated into Soldier.nUnk436. Burning cells also damage their item list through
 * Unit_ApplyCellDamage with power 20 and apObjects[42] as the weapon record.
 *
 * The face loop (0x4C048..0x4C210) makes six passes over the four abFaceDamage counters.
 * Passes 4 and 5 are the WEST and NORTH neighbour cells, gated on that neighbour burning, and
 * both rewrite the slot index to 1 / 2 (0x4C09C, 0x4C0EC) -- so they charge the wall A / wall B
 * counter through the wall A / wall B sprite, the same one the cell's own wall pass uses.
 *
 * The heat step is 20 when `bFlags36 & 1` and 5 otherwise, and the value the sprite is tested
 * against is `counter + step` (0x4C10C / 0x4C110), not the step. If `counter + step` is at or
 * below the sprite's bStrength the term is zeroed (0x4C160) and the counter is left alone; if
 * bStrength is below it the sprite is replaced by its Rec_GetExtraSpr variant (the floor sprite
 * only when its bExtraSprFacing is non-zero) and the counter is still left alone. The counter
 * only grows when there is no sprite at all -- or when bStrength is 255 and counter + step
 * exceeds 255 -- and then it grows by counter + step again, i.e. to 2 * counter + step,
 * saturating at 255 (0x4C1F0). That doubling is the original's arithmetic, not a port choice.
 *
 * Finally the countdowns: nFlags40 bits 3..6 decrement when non-zero and not already 0xF;
 * the low 3 bits of nFlags40 decrement, and when they are already 0 the burning state in
 * nFlags32 decrements and nFlags40's low 3 bits re-arm to `rand(2) + 2`.
 *
 * ## AfterEnemyStep (0x4c4a0) -- the post-enemy world tick
 *
 * 1. `nEnemyUnitCount / 3` times: pick a random enemy and raise uTu to `auStat[0] * 0.4`
 *    (a soft-float compare, an integer store) -- it never lowers uTu.
 * 2. Every cell of all four layers: MapCell_TickProximityItems, then bFlags44 bit 0 cleared.
 * 3. Every enemy holding a right-hand item with nUnitType > 3: the item's bUnk8 is cleared,
 *    the hand is emptied and the item goes back to apBelt[0].
 * 4. Every enemy: Soldier.bUnk148 (+0x94) = 0.
 * 5. Every XCom unit: uTu = auStat[0], then the burden penalty. Read off 0x4C77C..0x4C7C0 the
 *    grouping is
 *        q = (4 * uInvBurden - 4) / auStat[8]        (signed _rt_sdiv)
 *        d = (2 * q * auStat[0]) / 10
 *        uTu = auStat[0] - d
 *    auStat[8] is the mean-stun stat; Hex-Rays prints it as auStat[offsetof(Soldier,
 *    uStunOrArmor)], which reads like auStat[9] and is not. Then
 *    `uStunOrArmor += auStat[9] * auStat[8] / (10 * uInvBurden)`, capped at auStat[9].
 * 6. Tact_UpdateUnitControl for every XCom unit, then Map_RefreshCellMeta and
 *    UnitList_ForEachRefresh.
 * 7. Tact_UpdateUnitControl for every civilian.
 * 8. Every XCom unit with bMorale < 20 runs the panic AI, EnemyAi_TryFireOrWander.
 * 9. Unit_UpdateVisibility for every enemy.
 * 10. TactState_KickQueue, Map_RefreshCellMeta, UnitList_ForEachRefresh.
 * 11. Soldier.bUnk1037 = 0 for every XCom unit.
 *
 * Step 5 divides by `auStat[8]` and by `uInvBurden` with no zero guard, and `4 * uInvBurden - 4`
 * is negative when the burden is 0, so the signed divide can push uTu above auStat[0]. The port
 * skips both terms when the divisor is 0. Recorded in the bug ledger.
 */
struct EnemyAiActCtx;   /* pufo_enemyai.h; forward-declared because that header includes
                         * this one, so it cannot be included back here. */

typedef struct EnemyStepCtx {
    /* Seams. Each needs a context the caller owns: the proximity tick needs the blast and
     * focus context, the visibility refresh needs the LOS tables and the roster block, and
     * Map_RefreshCellMeta needs the lightmap. */
    void (*pfnTickProximity)(MapCell *pCell, void *pUser);  /* MapCell_TickProximityItems @ 0x4b724 */
    void (*pfnRefreshCells)(void *pUser);                   /* Map_RefreshCellMeta @ 0x2c7f4 */
    void (*pfnRefreshVisibility)(void *pUser);              /* UnitList_ForEachRefresh @ 0x22b54 */
    void (*pfnUpdateVisibility)(Soldier *pUnit, void *pUser); /* Unit_UpdateVisibility @ 0x48580 */
    u32  (*pfnRandU16)(u32 nMax, void *pUser);              /* Game_RandU16 @ 0x3e59c */
    void *pUser;

    MapCellLookup pfnLookup;
    const MapGeom *pGeom;                 /* nMapDimX @ 0x128AD3, nMapDimY @ 0x128AD4 */
    TactState     *pQueue;                /* pMapTactical @ 0x157198 */
    TactSelectCtx *pSelect;               /* Tact_SelectUnit @ 0x2c6c4 */
    StatusStack   *pStatus;               /* StatusMsg_Push @ 0x2dda4 */
    const StatusHooks *pStatusHooks;
    const char    *pTextRecs;             /* aTextRecs @ 0xB38D0, PUFO_TEXT_REC_STRIDE apart */
    const ObjectRecFull *pFireWeapon;     /* Map_apObjects42 @ 0x113B68, the fire tick's weapon */
    const struct EnemyAiActCtx *pActCtx;  /* EnemyAi_TryFireOrWander @ 0x4abc8 */
    RmpData       *pRmp;                  /* pRmpMissionData @ 0x15881C */

    Soldier **apXcom;      i32 *pnXcom;   /* apXcomUnits @ 0x128B0C, nXcomUnitCount @ 0x128AD7 */
    Soldier **apEnemy;     i32 *pnEnemy;  /* apEnemyUnits @ 0x128B84, nEnemyUnitCount @ 0x128ADC */
    Soldier **apCiv;       i32 *pnCiv;    /* apCivilians, nCivilianUnitCount @ 0x128AD8 */
    Soldier **ppHidden;                   /* apHiddenSoldiers @ 0x138DF0 */
    i32      *pnReserveCount;             /* Map_nEnemyReserveCount @ 0x128AFC */
    const u8 *pbMissionType;              /* Map_nMissionType @ 0x128AD2 */
    Soldier **ppSelUnit;                  /* Map_pSelUnit @ 0x128CEC */
    /* Original roster changes publish Map's byte counts immediately. The shell
     * can publish its i32 mirrors here before same-pass proximity/visibility use. */
    void (*pfnPublishRosterCounts)(i32 nXcom, i32 nEnemy, void *pUser);
} EnemyStepCtx;

/* aTextRecs @ 0xB38D0 record indices, from the byte offsets Tact_UpdateUnitControl loads:
 * 7995 = 205*39, 8200 = 205*40, 10660 = 205*52, 10865 = 205*53. Each is appended straight
 * after the unit's name, and each pair is the male/female verb form. In the shipped
 * lang.dat records 52 and 53 hold the same string. */
#define PUFO_TEXT_REC_CAME_TO_M      39  /* " came to" */
#define PUFO_TEXT_REC_CAME_TO_F      40  /* " woke up" */
#define PUFO_TEXT_REC_RELEASED_M     52  /* " released from alien control" */
#define PUFO_TEXT_REC_RELEASED_F     53  /* the female record, same text in lang.dat */

void Tact_UpdateUnitControl(Soldier *pUnit, const EnemyStepCtx *pCtx);
void BeforeEnemyStep(TactCtx272 *pTact, const EnemyStepCtx *pCtx);
void AfterEnemyStep(TactCtx272 *pTact, const EnemyStepCtx *pCtx);

/* The flag that makes a unit ineligible to act, from the opportunity-fire walk
 * (`(bFlags & 0x20) == 0` continues; set means skip). Tact_UpdateUnitControl reads it the
 * other way round: the recovery arm needs it SET, both side transfers need it CLEAR.
 * Recorded in the ledger as an original inconsistency. */
#define PUFO_UNIT_FLAG_CANNOT_ACT 0x20u

/* AfterEnemyStep tops up this many random enemies to 40% of their max TU. The mission type
 * that gets the hidden-reserve reinforcement is PUFO_MISSION_TYPE_XCOM_BASE in
 * pufo_tactctl.h. */
#define PUFO_ENEMYSTEP_TU_TOPUP_DIV 3
/* The top-up target is 40% of auStat[0]. The constant pair at 0x4C520/0x4C528 is
 * 0x3FD99999 / 0x9999999A, which is 0.4; Hex-Rays prints the two halves separately. */
#define PUFO_ENEMYSTEP_TU_TOPUP_SCALE 0.4

#endif /* PUFO_TURN_H */
