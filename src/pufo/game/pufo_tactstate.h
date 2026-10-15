/* pufo_tactstate.h - the tactical action queue.
 *
 * Evidence: TactState_ctor @ 0x4f2d4, TactState_EnqueueTurn @ 0x49b84,
 *           TactState_EnqueueStep @ 0x49d5c, TactState_EnqueueVertStep @ 0x49ddc,
 *           TactState_EnqueueStandFromKneel @ 0x49e60,
 *           TactState_EnqueueDeathFlash @ 0x4a29c,
 *           TactState_EnqueueRecover @ 0x4a37c,
 *           TactState_EnqueueClearAim @ 0x4a758,
 *           TactState_EnqueueEndMark @ 0x4f300,
 *           TactState_KickQueue @ 0x4f334,
 *           Unit_EnqueueIfApSpent @ 0x4a3d4.
 *
 * The whole tactical turn system works through this one queue: unit actions
 * append entries, and a driver consumes them from nQueueCursor while
 * bQueueActive is set. The original keeps it in the global unk_157198 @ 0x157198.
 *
 * Layout facts from the IDB TactState (70120 bytes):
 *   aQueue         @ 0x18,  1250 entries of 56 bytes
 *   nQueueCursor   @ 0x10
 *   nQueueLen      @ 0x111d4 (u16)
 *   bQueueActive   @ 0x111d6
 *   Only seven command codes are reachable from the enqueue helpers; the rest of
 *   the space is used by the driver, which is not ported yet.
 */
#ifndef PUFO_TACTSTATE_H
#define PUFO_TACTSTATE_H

#include "game/pufo_ballist.h"
#include "game/pufo_roster.h"   /* SideLists, for Tact_ProcessFallAction's side-list removals */
#include "game/pufo_tactctl.h"  /* TactSelectCtx, MapCursorPos */
#include "game/pufo_unit.h"
#include "pufo_common.h"

/* Documented for reference; not enforced as a size, because the Win64 entry is
 * larger by design. See the TactQueueEntry comment. */
#define PUFO_TACT_ENTRY_SIZE_ILP32 56
#define PUFO_TACT_QUEUE_MAX        1250

/* Command codes, each pinned to the function that emits it. */
typedef enum TactCmd {
    TACTCMD_TURN             = 0,  /* EnqueueTurn @ 0x49b84 */
    TACTCMD_STEP             = 1,  /* EnqueueStep @ 0x49d5c */
    TACTCMD_VERT_STEP        = 5,  /* EnqueueVertStep @ 0x49ddc */
    TACTCMD_STAND_FROM_KNEEL = 7,  /* EnqueueStandFromKneel @ 0x49e60 */
    TACTCMD_DEATH_FLASH      = 8,  /* EnqueueDeathFlash @ 0x4a29c */
    TACTCMD_CLEAR_AIM        = 10, /* EnqueueClearAim @ 0x4a758 */
    TACTCMD_RECOVER          = 11, /* EnqueueRecover @ 0x4a37c */
    TACTCMD_END_MARK         = 12, /* EnqueueEndMark @ 0x4f300 */
    TACTCMD_THROW            = 17, /* TactState_EnqueueThrow @ 0x4b2f4 */
    TACTCMD_ACTION18         = 18  /* Tact_EnqueueActionOrStatus205 @ 0x4b420 */
} TactCmd;

/* Queue entry. The original's entry is 56 bytes on ILP32 with these offsets:
 *     nCmd 0x00, pUnit 0x04, nFacing 0x08, nX 0x09, nY 0x0a, nTurnDir 0x0b,
 *     nDelta 0x0c, nZ 0x18, pWeapon 0x1c, nArg 0x2c
 *
 * The Win64 size is LARGER and the offsets after pUnit shift, because pUnit and
 * pWeapon widen from 4 to 8 bytes. That is deliberate and harmless: the queue is
 * runtime-only state rebuilt per mission. It is never serialised --
 * Save_WriteSoldiers @ 0x42350 writes soldiers field by field and no Save_*
 * function references unk_157198 -- so only field access semantics need to match,
 * not the byte layout. Forcing ILP32 packing here would buy nothing and would
 * make the struct lie about its own pointers. */
typedef struct TactQueueEntry {
    u8       nCmd;     /* 0x00 ILP32 */
    Soldier *pUnit;    /* 0x04 ILP32 */
    u8       nFacing;  /* 0x08 ILP32 */
    u8       nX;       /* 0x09 ILP32 */
    u8       nY;       /* 0x0a ILP32 */
    i8       nTurnDir; /* 0x0b ILP32 */
    i32      nDelta;   /* 0x0c ILP32 */
    u8       nZ;       /* 0x18 ILP32 */
    void    *pWeapon;  /* 0x1c ILP32 */
    u8       nArg;     /* 0x2c ILP32: the death-flash argument, IDA gap_2C[0] */
    /* Fields only the fire burst writes (ILP32 0x20, 0x24, 0x28 of the
     * 56-byte entry, which IDA left as separate dwords). */
    i32      nArgDword; /* 0x20 ILP32, the mode-7/fire argument */
    i32      nTuCost;   /* 0x24 ILP32, charged on the first shot only */
    i32      nMode;     /* 0x28 ILP32 */
    Soldier *pTarget;   /* 0x30 ILP32, written by command 18 */
} TactQueueEntry;

/* The modelled part of TactState. The real struct also carries ~70 KB of step
 * scratch (not yet ported) which is irrelevant to the queue itself. The field
 * offsets below are the original's, given so the ordering intent is recorded. */
/* Field-for-field from the IDB's TactState (70120 bytes). The scratch fields
 * after the queue are the ones the phase animations and Tact_ProcessMoveAction
 * write; they are named here for the first time. The queue sits at 0x18 in the
 * original and at the end here, which is harmless: a TactState is heap-allocated
 * through g_pTactState and is never serialised or shared with the original. */
typedef struct TactState {
    u8             bFlag0;           /* 0x0000 */
    u8             bFlag1;           /* 0x0001 */
    u8             gap_0002[2];      /* 0x0002 */
    i32            nStepFrameDiv;    /* 0x0004 */
    MapCell       *pStepFromCell;    /* 0x0008 */
    i32            nStepZDelta;      /* 0x000C */
    i32            nQueueCursor;     /* 0x0010 */
    u8             gap_0014[4];      /* 0x0014 */
    i32            nUnk70024;        /* 0x11188 */
    i32            nPhaseCounter;    /* 0x1118C, IDA nPhaseCounter */
    u8             gap_70032[12];    /* 0x11190 */
    i32            nStepDrawOffX;    /* 0x1119C */
    i32            nStepDrawOffY;    /* 0x111A0 */
    i32            nStepCellScratch; /* 0x111A4 */
    u8             gap_70056[8];     /* 0x111A8 */
    i32            nStepHeightSrc;   /* 0x111B0 */
    i32            nStepHeightDst;   /* 0x111B4 */
    u8             gap_70072[4];     /* 0x111B8 */
    i32            nTraceX;          /* 0x111BC */
    i32            nTraceY;          /* 0x111C0 */
    i32            nTraceZ;          /* 0x111C4 */
    u8             gap_70088[12];    /* 0x111C8 */
    u16            nQueueLen;        /* 0x111D4 */
    u8             bQueueActive;     /* 0x111D6 */
    u8             bUnk70103;        /* 0x111D7 */
    i32            nPhaseTick;       /* 0x111D8 */
    i32            nPhaseMax;        /* 0x111DC */
    u8             gap_70112[2];     /* 0x111E0 */
    u8             bUnk70114;        /* 0x111E2 */
    u8             bPhaseActive;     /* 0x111E3, IDA bUnk70115 */
    u8             bUnk70116;        /* 0x111E4 */
    u8             gap_70117[3];     /* 0x111E5 */
    TactQueueEntry aQueue[PUFO_TACT_QUEUE_MAX];
} TactState;

/* The original's global. */
extern TactState *g_pTactState;

void TactState_ctor(TactState *pState);

void TactState_EnqueueTurn(TactState *pState, Soldier *pUnit, u8 nFacing);

/* Unit_FaceToward @ 0x49bf0. Facing is not stored on the unit here: the original
 * turns the unit by enqueueing a command-0 entry. It reads the unit's own cell,
 * does nothing when (nX,nY) is that same cell, and otherwise appends the turn.
 * The queue is an argument because the original reaches it through unk_157198. */
void Unit_FaceToward(TactState *pState, Soldier *pUnit, i32 nX, i32 nY);
void TactState_EnqueueStep(TactState *pState, Soldier *pUnit, u8 nFacing, i32 nDelta);
void TactState_EnqueueStandFromKneel(TactState *pState, Soldier *pUnit);
void TactState_EnqueueVertStep(TactState *pState, Soldier *pUnit, u8 nFacing);
void TactState_EnqueueRecover(TactState *pState, Soldier *pUnit);
void TactState_EnqueueClearAim(TactState *pState, Soldier *pUnit);
void TactState_EnqueueEndMark(TactState *pState);

/* TactState_EnqueueThrow @ 0x4b2f4. When the unit can afford nTuCost it faces
 * (nX, nY) and appends a command-17 entry carrying the unit, coordinates, weapon,
 * single-byte argument and TU cost. Returns false (and writes nothing) when the
 * unit is NULL or cannot afford the cost; the original pushes the shared
 * out-of-TU status message itself, which the port leaves to the caller. */
bool TactState_EnqueueThrow(TactState *pState, Soldier *pUnit, TactItem *pItem,
                            u8 nX, u8 nY, u8 nZ, i32 nTuCost, i32 nArg,
                            void (*pfnFaceToward)(Soldier *pUnit, i32 nX, i32 nY,
                                                  void *pUser),
                            void *pUser);

/* Tact_EnqueueActionOrStatus205 @ 0x4b420. The command-18 psi/special enqueue:
 * when the unit can afford nTuCost it appends an entry carrying the unit, the
 * target unit, the weapon, the TU cost and the mode. Same status-message
 * deviation as TactState_EnqueueThrow. */
bool TactState_EnqueueAction18(TactState *pState, Soldier *pUnit, Soldier *pTarget,
                               TactItem *pItem, i32 nTuCost, i32 nMode);

/* The two door-interaction command codes Map_QueueDoorAction emits, plus the
 * leading open command 2 it shares with the other action paths. */
#define TACTCMD_OPEN  2u
#define TACTCMD_DOOR  3u
#define TACTCMD_STEP_DOOR 4u

/* Map_QueueDoorAction @ 0x2bd00.
 *
 * Given the unit's cell (`pCellA`) and the clicked cell (`pCellB`) plus the
 * footprint corner selector `nDir` (0/2/4/6), it picks the wall sprite to test
 * (wall-A for the mode-2 corners, wall-B for the others), then:
 *   - if the sprite's byte 0x2F is set, queues an open (cmd 2) and a door (cmd 3)
 *     action carrying the facing and the cell coordinates;
 *   - else, when the sprite blocks movement (byte 0x2A) and the target cell is not
 *     already flagged, scans the facing direction up to 3 cells back and 7 forward
 *     for the door span and queues a step-door (cmd 4) action carrying the turn
 *     distance and the far cell.
 * Returns true when an action was queued. Cells come from the injected
 * `pfnLookup`; `bSelUnitLarge` is `Unit_bIsLarge(pSelUnit)`. */
typedef struct DoorActionCtx {
    Soldier      *pSelUnit;
    bool          bSelUnitLarge;
    MapGeom       geom;
    MapCellLookup pfnLookup;
    void         *pUser;
} DoorActionCtx;

bool Map_QueueDoorAction(TactState *pState, MapCell *pCellA, MapCell *pCellB,
                         i32 nDir, const DoorActionCtx *pCtx);

/* TactState_EnqueueDeathFlash @ 0x4a29c.
 *
 * This one does NOT append. It inserts immediately after the entry currently
 * being executed (nQueueCursor + 1) so a death animation interrupts the active
 * action rather than queueing behind everything else. Two cases take the plain
 * append path instead:
 *   - the queue is not active, or
 *   - the cursor is already at the last entry (nothing to interrupt)
 * When inserting, entries from the end down to cursor+2 are shifted up by one to
 * make room, and nQueueLen is set to cursor+1 before the write. */
void TactState_EnqueueDeathFlash(TactState *pState, Soldier *pUnit, u8 nArg);

/* TactState_EnqueueFireBurst @ 0x4b080.
 *
 * The weapon scalars are passed separately because `TactItem.pObj` is the minimal
 * `ObjectRec` in pufo_mapcell.h, which has no `bAmmoCap`; the full record is
 * `ObjectRecFull` in pufo_boot.h. 255 means unlimited ammo. */
typedef struct FireBurstSpec {
    Soldier *pUnit;
    void    *pWeapon;       /* stored into the entries, not dereferenced here */
    i32      nMode;
    i32      nX, nY;
    u8       nZ;
    i32      nTuCost;       /* charged to the FIRST shot only */
    i32      nArg;          /* pCtxBase.nUnk40 at call time, written to every entry */
    u16      uAmmoInWeapon; /* pWeapon->nAmmo */
    u8       bAmmoCap;      /* pWeapon->pObj->bAmmoCap; 255 = unlimited */
    /* Mode 7 (multi-target) takes each shot's coordinates from apMultiTargets[i]
     * instead of the single target passed above. */
    MapCell **apMultiTargets;
    i32       nMultiTargetCount;
} FireBurstSpec;

/* TactState_EnqueueFireBurst @ 0x4b080.
 *
 * Verified behaviour:
 *
 *   1. If the unit's `uTu < nTuCost`, push the shared out-of-TU status message and
 *      do nothing else. The comparison is strict, so uTu == nTuCost is allowed.
 *   2. Face the unit toward the target (Unit_FaceToward @ 0x49bf0, injected).
 *   3. Work out the shot count:
 *        mode 7  -> nMultiTargetCount
 *        mode 0  -> 3
 *        else    -> 1
 *      and, except for mode 7, cap it at the weapon's ammo unless the capacity byte
 *      is 255 (unlimited).
 *   4. For each shot, append TWO queue entries: a command 14 (shot) with all the
 *      parameters, then a command 15 with just the unit. nQueueLen therefore grows
 *      by TWO per shot. Only the FIRST shot carries `nTuCost`; the rest get 0.
 *   5. Append a command 10 (clear aim) for the unit.
 *
 * Command 14's coordinates come from apMultiTargets[i] in mode 7 and from the single
 * (nX, nY, nZ) argument otherwise.
 *
 * The original also clears three context scratch fields (two inside the big context
 * and one byte of TactState) that belong to the renderer and the shot animator. The
 * port leaves those to the caller and documents them (deviation 1). */
bool TactState_EnqueueFireBurst(TactState *pState, const FireBurstSpec *pSpec,
                                void (*pfnFaceToward)(Soldier *pUnit, i32 nX, i32 nY,
                                                      void *pUser),
                                void *pUser);

/* The command codes the burst uses. Declared beside the enum's other values because
 * they are only emitted here. */
#define TACTCMD_FIRE_SHOT 14u
#define TACTCMD_FIRE_MARK 15u

/* TactState_KickQueue @ 0x4f334. Starts consuming: cursor 0, active set, and
 * both scratch flags cleared. No-op when the queue is empty. */
void TactState_KickQueue(TactState *pState);

/* Unit_EnqueueIfApSpent @ 0x4a3d4.
 *
 * If the unit has spent at least uHp time units and does not have flag 0x20 set,
 * it is finished: a death flash is queued unless it is a large unit that still
 * has HP, and the queue is kicked if it was not already running.
 * Note the comparison mixes uTuSpent (time units) with uHp; that is what the
 * original does and it is intentional, not a typo. */
void Unit_EnqueueIfApSpent(TactState *pState, Soldier *pUnit, u8 nArg);

/* GameTick_Step @ 0x27324.
 *
 * The per-frame tick for animated tiles and the enemy-turn phase counter.
 *
 *  1. Do nothing unless bPhaseActive is set.
 *  2. Sweep every cell on every layer. For any cell whose nFlags32 has a
 *     non-zero bits 3..6 field (mask 0x78), decrement that field:
 *         new = ((((u8)((v9 & 0xF8) - 1)) ^ (u8)v9) & 0x78) ^ v9
 *     The mask isolates bits 3..6 so the decrement cannot borrow into the
 *     surrounding flag bits; the field wraps 0 -> 0xF rather than saturating.
 *  3. Increment nPhaseCounter. Past 11, clear bPhaseActive and stop.
 *
 * The original also sets and clears pCtxBase.bEnemyTurnActive around this, and
 * calls Render_Frame on the final step. Neither belongs here: the port sets the
 * enemy-turn flag from its own phase owner and renders from the frame loop.
 *
 * Returns true when the phase ended on this call. */
bool GameTick_Step(TactState *pState, MapCellLookup pfnLookup, void *pUser);

/* Applies the animated-tile decrement to one cell. Exposed so the bit arithmetic
 * can be tested in isolation against the original's expression. */
u32 GameTick_DecayCellFlags(u32 nFlags32);

/* Phase length in ticks, from GameTick_Step's `> 11` guard. */
#define PUFO_PHASE_TICK_MAX 11

/* --- Tact_ProcessMoveAction @ 0x4f3f0 --- */

/* aTextRecs @ 0xb38d0 is a table of 205-byte records. The move FSM pushes three
 * of them through StatusMsg_Push @ 0x2dda4. */
#define MOVE_MSG_NO_TIME     1 /* 0xb399d, "not enough time" */
#define MOVE_MSG_ARMOUR_BLOCK 2 /* 0xb3a6a, armour would leave too little TU */
#define MOVE_MSG_THROW_FAIL  4 /* 0xb3c04, the throw found no landing */

/* The move FSM reaches into the original's context block (pCtxBase @ 0x1139b8)
 * for scratch globals, the unit rosters and the shot plumbing. The port injects
 * all of it; every field names the context offset it stands for. */
typedef struct MoveActionCtx {
    MapGeom       geom;        /* nMapDimX @ 0x1511B, nMapDimY @ 0x1511C */
    MapCellLookup pfnLookup;
    void         *pUser;

    /* Scratch globals. */
    i8          *pnLayerScratch;  /* 0x15138 unk_128AF0, signed byte */
    i32         *pnMaxLayerClamp; /* 0x15074 unk_128A2C */
    i32         *pnAnimPhase;     /* 0x10F8 unk_113AB0 */
    i32         *pnShotCount;     /* 0x2C  unk_1139E4, Hex-Rays nStatusDrawY */
    u8          *pbActionLatch;   /* 0x02  unk_1139BA, the action latch */
    u8          *pbFireStatus1;   /* 0x51  unk_113A09 */
    TactItem    **ppSelWeapon;    /* 0x48  unk_113A00 */
    Soldier     **ppThrowTarget;  /* 0x150F0 unk_128AA8 */
    i32         *pnUnk1405C;      /* 0x1405C unk_127A14 */
    bool         bAnimBusy;       /* unk_157180 -> +0xF8, the walk-sound gate */
    Soldier     *pTrackUnit;      /* unk_157180 -> +0x10C, ditto */
    MapCell    **apShotCells;     /* the shot-history cell array; the original
                                   * reads apShotCells[nShotCount] as
                                   * *(pCtxBase + 4 * nShotCount + 4) at 0x50008 */

    /* Rosters: three 30-entry pointer arrays and their counts. */
    Soldier     **apXcomUnits;    /* 0x15154 */
    Soldier     **apEnemyUnits;   /* 0x151CC */
    Soldier     **apCivilians;    /* 0x15244 */
    u8          *pnXcomCount;     /* 0x1511F unk_128AD7, byte */
    u8          *pnEnemyCount;    /* 0x15124 unk_128ADC, byte */
    i32         *pnCivilianCount; /* 0x15120 unk_128AD8, dword */

    /* Shot plumbing. */
    AimState          *pAim;      /* the Map aim block, 0x13FD8..0x14088 */
    const LosTables   *pTables;
    ScatterOffset     *pScatter;  /* 0x15058/5C/60 unk_128A10/14/18 */
    const double      *pAdMtX;    /* 0x13EE8, 10 entries */
    const double      *pAdMtY;    /* 0x13F38, 10 entries */
    const double      *pAdMtZ;    /* 0x13F88, 10 entries */
    ThrowArc          *pThrowArc; /* scratch the two throw calls share */
    ThrowLanding      *pThrowLanding;
    const ThrowBudget *pThrowBudget;
    const InvStatHooks *pInvHooks;    /* for Unit_RecalcInventoryStat */
    bool         bThrowHitStop;   /* Map.bTrajHitStop @ 0x1406D as the throw
                                   * landing call sees it */
    Soldier           *pTraceExcludeSel;   /* 0x1406C/0x14070 exclusions the arc
                                            * trace reads; the move FSM forwards
                                            * whatever the context already holds */
    Soldier           *pTraceExcludeExtra;

    /* Seams the port has not yet wired to a context object. */
    bool (*pfnArmorTuWarn)(const Soldier *pUnit, i32 nTuCost, void *pUser);
    void (*pfnPushStatus)(i32 nTextRecIndex, void *pUser);
    void (*pfnUpdateVisibility)(Soldier *pUnit, void *pUser);
    void  *pCallbackUser;
    VisCtx *pVisCtx;              /* cmd 0xB ends with Unit_UpdateVisibility */
} MoveActionCtx;

/* The original returns an int, but every exit is either a register leftover or
 * the result of Tact_RefreshTeamVisibility, which the port declares void. The
 * port therefore returns nothing; no caller in the original tests the value in a
 * way the port can reproduce. */
void Tact_ProcessMoveAction(TactState *pState, const MoveActionCtx *pCtx);

/* --- Tact_ProcessFallAction @ 0x52adc and Unit_ApplyFallDamage @ 0x4a7b0 -----
 *
 * The third of the three queue processors. Tact_TickActiveUnit @ 0x54130 runs
 * ProcessFire or ProcessMove for the entry at nQueueCursor and then this one when
 * TactState.bFlag1 is set; ProcessFallAction clears bFlag0 and advances the cursor
 * at its tail (0x540E0), and a set Map_bTickHold skips the advance entirely.
 *
 * The switch is on nCmd - 1 with 18 slots and handles nCmd 1, 3, 4, 5, 8, 0xA,
 * 0xC, 0xE, 0xF, 0x10, 0x11 and 0x12; nCmd 2, 6, 7, 9, 11 and 13 fall to the
 * default and only advance the cursor. Despite the IDB name it is not only the
 * fall case: nCmd 0xF (the Unit_ApplyFallDamage sweep) is one case of twelve.
 *
 * Case 0x12 is the resolution of the command-18 action. It branches on the weapon
 * record's uSortKey (23 = the look/mind-control device, 21 = the medikit) and then
 * on the queue entry's nMode: 5 and 4 and 10 are the psi attacks, 6 is the human
 * melee (TactAction_ConfirmMeleeMode6 @ 0x17bb8 sets exactly that mode), 13 is the
 * alien melee resolved by the attacker's unit type. Which psi variant is which is
 * not established by the code; they differ only in the morale constant (600, 1000,
 * 600) and in what happens to the victim.
 */

/* TEXTS records this pair pushes, by aTextRecs @ 0xb38d0 index (stride 205). The
 * address in the comment is the original's raw pointer into the loaded block. */
#define PUFO_TXTREC_SPOTTED        3  /* unk_B3B37  "Я вижу врага" */
#define PUFO_TXTREC_WOUNDS_HEALED  8  /* 1640       "Все смертельные раны излечены" */
#define PUFO_TXTREC_PART_HEALED   13  /* 2665       "частично поправился" */
#define PUFO_TXTREC_MORALE_BACK   14  /* 2870       "мораль восстановлена" */
#define PUFO_TXTREC_CONSCIOUS     46  /* 9430       "Пришел в себя" */
#define PUFO_TXTREC_PSI_OK        37  /* unk_B5671  "Пси атака успешна" */
#define PUFO_TXTREC_PSI_FAILED    38  /* unk_B573E  "Пси атака не удалась" */
#define PUFO_TXTREC_ALIEN_CONTROL 54  /* unk_B640E  " под контролем пришельцев" */

/* The psi power test. Both products are auStat[6] * auStat[7]; the victim's is
 * floored at PUFO_FALL_POWER_MIN before it divides the attacker's, so a weak
 * attacker against a weak victim gives ratio 1 and the attack cannot fail. The
 * roll is Game_RandU16(150) against 100 / ratio. */
#define PUFO_FALL_POWER_MIN     1600
#define PUFO_FALL_PSI_RAND_MAX   150
#define PUFO_FALL_PSI_PERCENT    100
#define PUFO_FALL_MORALE_PANIC    50  /* morale > 50 stops short of the panic tail */
#define PUFO_FALL_MORALE_MAX     100

/* TU costs and fixed hits. */
#define PUFO_FALL_TU_STEP          4  /* nCmd 4, charged on the queue entry's unit */
#define PUFO_FALL_TU_VERT          8  /* nCmd 5 */
#define PUFO_FALL_TU_MELEE        20  /* case 0x12 LABEL_93, charged on the attacker */
#define PUFO_FALL_MORALE_DIVISOR 150  /* attacker product / -150 */
#define PUFO_FALL_PSI_REACT_600  600  /* modes 5 and 10 */
#define PUFO_FALL_PSI_REACT_1000 1000 /* mode 4 */
#define PUFO_FALL_CELACID_BASE    90  /* the celacid power before the roll */
#define PUFO_FALL_FIXED_HIT       80  /* silacoid and riper, and the uHp+80 stun */
#define PUFO_FALL_CELACID_TU     100  /* the celacid's uTuSpent charge */

/* The thrown-item landing search descends at most this many layers. */
#define PUFO_FALL_THROW_DESCEND    3
#define PUFO_FALL_SOUND_ITEM_LANDED 13

/* The unit types case 0x12 mode 13 special-cases, and the type the chryssalid
 * transform writes. Types 16 and 17 are the two civilians the transform accepts. */
#define PUFO_FALL_TYPE_CHRYSSALID 14
#define PUFO_FALL_TYPE_CELACID    15
#define PUFO_FALL_TYPE_SILACOID   19
#define PUFO_FALL_TYPE_RIPER      20
#define PUFO_FALL_TYPE_TRANSFORMED 22

/* The cell-flag bits case 4 writes on the door span: bit 0 cleared on the near
 * cell, bit 2 set on the far one. Both lookups use the same nZ, so in the
 * original they are the same cell. */
#define PUFO_FALL_CELLFLAG_CLEAR   0x01u
#define PUFO_FALL_CELLFLAG_DOOR    0x04u

/* Unit_ApplyFallDamage @ 0x4a7b0.
 *
 * The fall is resolved in three steps, all of them reading the unit's own cell:
 *
 *   1. Three early returns: the cell has a floor sprite (MapCell_TestFlag 5), the
 *      unit's type has g_aUnitTypeFlag0 set (the levitating types - floater 11 and
 *      celacid 15 - are immune), or the unit is already on layer 0.
 *   2. A descent. nLayer starts at the unit's nZ and steps down while the tested
 *      cell has no floor sprite. A small unit tests its own column; a large unit
 *      tests the other three cells of its 2x2 footprint and descends only when all
 *      three are open (0x4A91C: the AND of the first two tests, then the third one
 *      through the shared LABEL_14). The loop stops at the first layer that HAS a
 *      floor, or at nLayer == -1 when the whole column is open. The three footprint
 *      cells the sweep then uses are the ones the loop last computed, so they sit on
 *      the stopping layer.
 *   3. damage = (int)((float)(nZ - nLayer - 1) * (float)auStat[1] * 0.4f), clamped
 *      at zero. The constant is the float 0x3ECCCCCD = 0.4f, and _itos / _muls /
 *      _stoi are coredll's soft-float helpers, not fixed point.
 *
 * Then the landing cells are swept - one for a small unit, the full 2x2 for a
 * large one - and every occupant is killed. A small unit that lands ON a large one
 * dies itself (0x4AAE4) without its HP being touched first. Finally, unless the
 * unit is already flagged dead, it is placed on the landing layer, loses the
 * damage from uHp with a floor at zero, and runs Unit_EnqueueIfApSpent. */
typedef struct FallDamageCtx {
    MapCellLookup   pfnLookup;
    void           *pUser;
    TactState      *pState;      /* pMapTactical @ 0x157198, for Unit_EnqueueIfApSpent */
    const KillCtx  *pKillCtx;    /* for the two Tact_KillUnit calls @ 0x4AB10, 0x4AB40 */
    const KillHooks *pKillHooks;
} FallDamageCtx;

void Unit_ApplyFallDamage(Soldier *pUnit, const FallDamageCtx *pCtx);

/* Tact_ProcessFallAction @ 0x52adc reaches the same context block MoveActionCtx
 * models, plus the rosters, the side lists and the object the case-0x12 branches
 * drive. Every field names the original offset it stands for. */
typedef struct FallActionCtx {
    MapGeom        geom;         /* nMapDimX @ 0x1511B, nMapDimY @ 0x1511C */
    MapCellLookup  pfnLookup;
    void          *pUser;

    /* Scratch globals. */
    i8            *pnLayerScratch;      /* nLayerScratch @ 0x128AF0 (Map+0x15138) */
    i32           *pnMaxLayerClamp;     /* nMaxLayerClamp @ 0x128A2C (Map+0x15074) */
    i32           *pnAnimPhase;         /* nAnimPhase @ 0x113AB0 (Map+0x10F8) */
    i32           *pnFireTickProgress;  /* nFireTickProgress @ 0x127A14 (Map+0x1405C) */
    u8            *pbEnemySpotted;      /* unk_118800 @ Map+0x4E48: set by
                                         * Unit_SpotCellOccupant @ 0x48344, cleared by
                                         * Unit_ScanVisibleUnits @ 0x4836C. Gates the
                                         * TEXTS[3] "I see the enemy" push. */
    u8            *pbFallDamagePending; /* byte_113A0A @ Map+0x52: arms the nCmd 0xF sweep */
    u8            *pbThrowPending;      /* byte_127A25 @ Map+0x1406D: arms the nCmd 0x11 landing */
    u8            *pbTickHold;          /* Map_bTickHold @ 0x113A12 */
    u16           *pnUiScreen;          /* Map_nUiScreen @ 0x1139F2 */
    TactItem      **ppSelWeapon;        /* pSelWeapon @ 0x113A00 (Map+0x48) */
    Soldier       **ppThrowTarget;      /* pThrowTargetUnit @ 0x128AA8 (Map+0x150F0) */
    Soldier       **ppLookCommitUnit;   /* pLookCommitUnit @ 0x128A40 (Map+0x15088) */
    u8            *pbLookCommitFlag;    /* bLookCommitFlag @ 0x128A44 (Map+0x1508C) */
    i32           *pnCursorX;           /* Map_nCursorX @ 0x128AE0 */
    i32           *pnCursorY;           /* Map_nCursorY @ 0x128AE4 */

    /* The melee target cell, written by TactAction_ConfirmMeleeMode6 @ 0x17bb8. */
    const i32     *pnMeleeCellX;        /* unk_1279F8 */
    const i32     *pnMeleeCellY;        /* unk_1279FC */
    const i32     *pnMeleeCellLayer;    /* unk_127A00 */

    /* The thrown item's flight end, in sub-cell units: 16 to a cell in X and Y,
     * 12 to a layer. Read through coredll's _dtoi. */
    const double  *pdThrowX;            /* unk_1278A8 */
    const double  *pdThrowY;            /* unk_1278F8 */
    const double  *pdThrowLayer;        /* unk_127948 */

    /* Rosters. The counts are the original's bytes; SideLists carries its own copy
     * by value, so Fall_SyncCounts writes the removal back. */
    Soldier      **apXcomUnits;         /* apXcomUnits @ 0x15154 */
    Soldier      **apEnemyUnits;        /* apEnemyUnits @ 0x151CC */
    u8            *pnXcomCount;         /* nXcomUnitCount @ 0x1511F */
    u8            *pnEnemyCount;        /* nEnemyUnitCount @ 0x15124 */
    SideLists     *pLists;              /* the same two rosters, for SideLists_RemoveUnit */

    /* Context objects for the callees this function drives. */
    struct TactCtx272 *pTactCtx;        /* pTactCtx272 @ 0x157180: bBusy, nPhase, pTrackUnit */
    VisCtx           *pVisCtx;          /* Unit_UpdateVisibility, UnitList_ForEachRefresh */
    KillCtx          *pKillCtx;         /* Tact_KillUnit @ 0x4E430 */
    KillHooks        *pKillHooks;
    struct ProximityCtx *pProximity;    /* Combat_DetonateNearbyItems @ 0x32FA8 */
    struct UnitRosters *pRosters;       /* UnitList_ForEachRefresh @ 0x22B54 */
    StatusStack      *pStatus;
    const StatusHooks *pStatusHooks;
    TactSelectCtx    *pSelectCtx;       /* Tact_SelectUnit @ 0x2C6C4 */
    MapCursorPos     *pCursorPos;       /* MapCursor_SetPos @ 0x27014 */
    const char       *pszTransformName; /* unk_B14B0: the chryssalid transform name */
    bool              bLightEnabled;    /* Map_RefreshCellMeta's light gate */
    const u8         *pLightmap;

    /* Seams. pfnPushStatus takes a TEXTS record index because the port has no raw
     * text-record accessor; pfnPushForcedText carries the one message the original
     * builds itself (the panicked unit's name plus PUFO_TXTREC_ALIEN_CONTROL) and
     * pushes it with bForce set. pszAlienControlSuffix is that record's text.
     * pfnUpdateVisibility, when set, stands for Unit_UpdateVisibility @ 0x48580 at
     * every call site here (including LABEL_111); pVisCtx is used when it is NULL. */
    void (*pfnUpdateVisibility)(Soldier *pUnit, void *pUser);
    void (*pfnTryOpportunityFire)(Soldier *pTarget, Soldier *pShooter, void *pUser);
    void (*pfnCheckBattleEnd)(u8 bAbort, void *pUser);
    void (*pfnPushStatus)(i32 nTextRecIndex, void *pUser);
    void (*pfnPushForcedText)(const char *pszMsg, void *pUser);
    const char *pszAlienControlSuffix;    /* unk_B640E */
    void  *pCallbackUser;
} FallActionCtx;

void Tact_ProcessFallAction(TactState *pState, const FallActionCtx *pCtx);

#endif /* PUFO_TACTSTATE_H */
