/* pufo_unit.h - soldier entities: identity, state predicates, placement, facing.
 *
 * Evidence: Soldier_ctor @ 0x476c8, Soldier_Generate @ 0x49410,
 *           Unit_ResetBattleState @ 0x498b0, Unit_bIsOperative @ 0x49eb8,
 *           Unit_bIsLarge @ 0x49ecc, Unit_PlaceOnMap @ 0x49a0c,
 *           Unit_FaceToward @ 0x49bf0, Soldier_RemoveById @ 0x36b8c,
 *           Tact_SelectUnit @ 0x2c6c4, Save_WriteSoldiers @ 0x42350.
 *
 * Soldier is 1044 (0x414) bytes in the original (ILP32). It is NOT serialised
 * as a raw blob: Save_WriteSoldiers @ 0x42350 writes every field individually,
 * in a fixed order, with a different layout for the non-tactical case. That is
 * why the port may use a Win64 struct without breaking save compatibility, and
 * why the struct is modelled with real field names plus a tail pad rather than
 * as a byte-exact image.
 *
 * The offset constants below are the PROVEN ILP32 offsets. On Win64 they shift
 * because pCell widens from 4 to 8 bytes, so anything that needs an offset uses
 * offsetof on this struct, guarded by the static asserts at the bottom.
 */
#ifndef PUFO_UNIT_H
#define PUFO_UNIT_H

#include "game/pufo_mapcell.h"
#include "pufo_common.h"

#define PUFO_SOLDIER_SIZE_ILP32 1044
#define PUFO_UNIT_STAT_COUNT    10

/* Unit type table, byte_A7B80 @ 0xa7b80. Stride 4, 24 rows (96 bytes; the byte
 * at +0x60 is not part of it). byte[+0] is the "flag 0" column and byte[+1] the
 * "is large" flag that Unit_bIsLarge @ 0x49ecc tests against 1.
 *
 * The table used to be 16 rows and the is-large column had 1s at types 11 and
 * 13. Both were wrong: those two rows are {1,0,1,0}, so flag 0 is set and the
 * unit is NOT large. Types 16..23 exist and are reachable - the switch in
 * Tact_ProcessMoveAction @ 0x4f3f0 has cases up to 0x16. */
#define PUFO_UNIT_TYPE_COUNT 24
extern const u8 g_aUnitTypeIsLarge[PUFO_UNIT_TYPE_COUNT];
extern const u8 g_aUnitTypeFlag0[PUFO_UNIT_TYPE_COUNT];

/* Unit muzzle-offset table, unk_A9768 @ 0xa9768: 23 blocks of 96 bytes, one per
 * unit type, each 2 groups of 8 facings, each entry 3 i16 (dx, dy, dz).
 *
 * Index proven from ARM at 0x50074..0x50094:
 *     idx = pUnit->bFacing + 8 * (bSel + 2 * pUnit->nUnitType)
 * where bSel is ObjectRecFull.bListPri (pObj+0xA4) clamped to 0 or 1, and the
 * byte offset is 6 * idx. The offsets are added to 16*cell->nX / 16*cell->nY /
 * 12*cell->nZ to get the shot origin in sub-cell units (0x50098..0x500e0).
 *
 * Types 14..22 are zero blocks except type 18 group 1, which is {0,0,-3} for all
 * eight facings. */
#define PUFO_UNIT_MUZZLE_TYPE_COUNT 23
typedef struct MuzzleOffset {
    i16 nDx;
    i16 nDy;
    i16 nDz;
} MuzzleOffset;
extern const MuzzleOffset g_aUnitMuzzleOffset[PUFO_UNIT_MUZZLE_TYPE_COUNT][2][8];

/* Unit_bIsOperative @ 0x49eb8 returns bStatus == 0; MapCell_HasOccupant
 * @ 0x5487c treats bStatus < 3 as occupying. */
typedef enum UnitStatus {
    UNIT_STATUS_OPERATIVE = 0,
    UNIT_STATUS_1         = 1,
    UNIT_STATUS_2         = 2,
    UNIT_STATUS_INACTIVE  = 3 /* >= 3: no longer occupies a cell */
} UnitStatus;

/* nUnitType values with special-cased armour, from Unit_ResetBattleState. */
#define PUFO_UNITTYPE_ARMOR_100    2
#define PUFO_UNITTYPE_ARMOR_150    3
#define PUFO_UNITTYPE_HUMANOID_MAX 8

/* SoldierAnim @ 0x1c: the animation view of SoldierMultiOrAnim, from the IDB
 * SoldierAnimTmp type (120 bytes). Only the two fields Unit_GetExtraSpr needs
 * are named; everything else stays opaque until its user is ported. The
 * original union also overlays a multi-target pointer array on this storage,
 * which the port will model when a consumer needs it. */
typedef struct SoldierAnim {
    u8  nPathZ;          /* 0x00 */
    u8  gap_1D[9];       /* 0x01 */
    u16 uExtraSprBase;   /* 0x0a */
    u8  gap_28[6];       /* 0x0c */
    u8  bUnk12;          /* 0x12, original Soldier +0x2E. Unit_UpdateVisibility
                        * @ 0x48580 treats 1 as 'the cell below may be revealed
                        * through this unit'. */
    u16 uFacingResetW;   /* 0x14 */
    u16 uFacingResetH;   /* 0x16 */
    u8  nFacingAnimA;    /* 0x18 */
    u8  nFacingAnimB;    /* 0x19 */
    u8  nFacingStored;   /* 0x1a */
    u8  gap_37[93];      /* 0x1b */
} SoldierAnim;
_Static_assert(sizeof(SoldierAnim) == 120, "SoldierAnim is 120 bytes");

/* The original's SoldierMultiOrAnim union overlays the animation state with the
 * 30-entry multi-target pointer array (120 bytes on ILP32). On Win64 the pointer
 * array is wider, so the union grows; that is harmless because a Soldier is never
 * serialised raw (Save_WriteSoldiers writes it field by field). */
#define PUFO_UNIT_MULTITARGET_MAX 30

typedef union SoldierMultiOrAnim {
    SoldierAnim anim;
    struct Soldier *apMultiTargets[PUFO_UNIT_MULTITARGET_MAX];
} SoldierMultiOrAnim;

/* The original's InvPocketPair: two item slots, two meta bytes and two spare.
 * Used by Soldier.aPockets[4]. */
typedef struct InvPocketPair {
    TactItem *apItem[2]; /* 0x0 */
    u8        abMeta[2]; /* 0x8 */
    u8        gap_A[2];  /* 0xa */
} InvPocketPair;

/* Soldier: 1044 (0x414) bytes in the original. The field order and the ILP32
offsets are from the IDB; offsets after pCell shift on Win64 because pointers
widen, so only the exact ILP32 numbers are asserted.

Note the struct is NOT serialised raw: Save_WriteSoldiers @ 0x42350 writes every
field individually, so the Win64 layout is free as long as field semantics match.
The one opaque region is the inventory block (pHandL..bHumanoidInv), which the
inventory work will model; carrying it as bytes keeps every offset after it
correct. */
typedef struct Soldier {
    i32        nAiTick;        /* 0x000 */
    u8         bFlags;         /* 0x004 */
    u8         bFacing;        /* 0x005 */
    u16        uTu;            /* 0x006 */
    u16        uStunOrArmor;   /* 0x008 */
    u16        uHp;            /* 0x00a */
    u8         bMorale;        /* 0x00c */
    u8         gap_0D[3];      /* 0x00d */
    MapCell   *pCell;          /* 0x010 */
    u8         nAnimPose;      /* 0x014 */
    i8         nDrawOffX;      /* 0x015 */
    i8         nDrawOffY;      /* 0x016 */
    u8         bStatus;        /* 0x017 */
    u16        uTuSpent;       /* 0x018 */
    i8         nMultiTarget;   /* 0x01a */
    u8         gap_1B;         /* 0x01b */
    SoldierMultiOrAnim multiOrAnim; /* 0x01c, original SoldierMultiOrAnim union */
    /* A unit cannot hold both a multi-target list and an active animation, so the
     * two views are one storage in the original and one union here. */
    u8         bUnk148;        /* 0x094 */
    u8         bUnk149;        /* 0x095 */
    u8         gap_150[2];     /* 0x096 */
    i32        nLook;          /* 0x098 */
    i32        bFemale;        /* 0x09c, stored as a dword */
    u8         nUnitType;      /* 0x0a0 */
    u8         gap_161;        /* 0x0a1 */
    u16        auStat[PUFO_UNIT_STAT_COUNT]; /* 0x0a2 */
    u8         nKind;          /* 0x0b6 */
    u8         bUnk183;        /* 0x0b7 */
    u16        uMissions;      /* 0x0b8 */
    char       szName[40];     /* 0x0ba */
    u8         bRankType;      /* 0x0e2 */
    u8         gap_227;        /* 0x0e3 */
    u16        auStatBase[9];  /* 0x0e4 */
    u8         bKneelOrPrimed; /* 0x0f6 */
    u8         bUnk247;        /* 0x0f7 */
    u8         bUnk248;        /* 0x0f8 */
    u8         gap_249;        /* 0x0f9 */
    u16        uKills;         /* 0x0fa */
    u8         abReady252[5];  /* 0x0fc */
    u8         gap_257[3];     /* 0x101 */
    /* Inventory block: pHandL @0x104 through abBeltMetaB, ending at 0x1a2.
     * aPockets is InvPocketPair[4] (2 items, 2 meta bytes, 2 spare each). */
    TactItem  *pHandL;            /* 0x104 */
    TactItem  *pHandR;            /* 0x108 */
    InvPocketPair aPockets[4];    /* 0x10c */
    TactItem  *apBackpack[8];     /* 0x13c */
    u8         abBackpackMeta[16];/* 0x15c */
    TactItem  *apBelt[9];         /* 0x16c */
    u8         abBeltMetaA[9];    /* 0x190 */
    u8         abBeltMetaB[9];    /* 0x199 */
    u8         bHumanoidInv;   /* 0x1a2 */
    u8         gap_419;        /* 0x1a3 */
    u16        uInvBurden;     /* 0x1a4 */
    u8         bUnk422;        /* 0x1a6 */
    u8         bUnk423;        /* 0x1a7 */
    i32        nArmorPoints;   /* 0x1a8 */
    i32        nClass;         /* 0x1ac */
    u8         bSpecialMark;   /* 0x1b0 */
    u8         gap_433;        /* 0x1b1 */
    /* Read as gap_433[1] by Tact_UpdateUnitControl @ 0x4a444: set once when the
     * unit is recovered from alien control, which makes the function idempotent. */
    u8         bControlRecovered; /* 0x1b2 */
    u8         bUnk435;        /* 0x1b3 */
    i32        nUnk436;        /* 0x1b4 */
    i32        nUnk440;        /* 0x1b8, the roster id used by Soldier_RemoveById */
    u8         bRankDirty;     /* 0x1bc */
    u8         gap_445[7];     /* 0x1bd */
    u8         bAiDone;        /* 0x1c4 */
    u8         gap_1C5[3];     /* 0x1c5 */
    Soldier   *apSpotted[60];  /* 0x1c8 */
    i32        nSpottedCount;  /* 0x2b8 */
    char       szDisplayName[40]; /* 0x2bc */
    u8         gap_740[216];   /* 0x2e4 */
    i32        nSaveId;        /* 0x3bc */
    u8         bInZone;        /* 0x3c0 */
    u8         bUnk961;        /* 0x3c1 */
    u16        auUnk962[25];   /* 0x3c2 */
    u8         abUnk1012[25];  /* 0x3f4 */
    u8         bUnk1037;       /* 0x40d */
    u8         bInTact;        /* 0x40e */
    u8         gap_40F;        /* 0x40f */
    void      *pLinked1040;    /* 0x410 */
} Soldier;

/* Proven ILP32 offsets. On Win64 the ones after pCell shift by 4, so only the
 * prefix up to pCell can be asserted on both targets. */
_Static_assert(offsetof(Soldier, bFacing) == 0x05, "Soldier.bFacing at 0x05");
_Static_assert(offsetof(Soldier, uTu) == 0x06, "Soldier.uTu at 0x06");
_Static_assert(offsetof(Soldier, uHp) == 0x0a, "Soldier.uHp at 0x0a");
_Static_assert(offsetof(Soldier, bMorale) == 0x0c, "Soldier.bMorale at 0x0c");
_Static_assert(offsetof(Soldier, pCell) == 0x10, "Soldier.pCell at 0x10");

/* bStatus is proven to sit right after pCell, so its offset tracks pointer
 * width. This assert is what makes PUFO_SOLDIER_BSTATUS_OFF safe to use as a raw
 * byte offset in MapCell_HasOccupant: if the struct layout ever drifts, the
 * build fails here rather than reading the wrong byte silently. */
_Static_assert(offsetof(Soldier, bStatus) == PUFO_SOLDIER_BSTATUS_OFF,
               "Soldier.bStatus must match PUFO_SOLDIER_BSTATUS_OFF");

#if UINTPTR_MAX == 0xFFFFFFFFu
_Static_assert(sizeof(Soldier) == PUFO_SOLDIER_SIZE_ILP32, "Soldier is 1044 bytes on ILP32");
_Static_assert(offsetof(Soldier, nAnimPose) == 0x14, "Soldier.nAnimPose at 0x14 on ILP32");
_Static_assert(offsetof(Soldier, multiOrAnim) == 0x1c, "Soldier.multiOrAnim at 0x1c on ILP32");
_Static_assert(offsetof(Soldier, uTuSpent) == 0x18, "Soldier.uTuSpent at 0x18 on ILP32");
_Static_assert(offsetof(Soldier, nUnitType) == 0xa0, "Soldier.nUnitType at 0xa0 on ILP32");
_Static_assert(offsetof(Soldier, auStat) == 0xa2, "Soldier.auStat at 0xa2 on ILP32");
#endif

/* Soldier_ctor @ 0x476c8 clears only the tail link. Called out because callers
 * must not assume a full zero-fill; see Soldier_Generate for the field-by-field
 * initialisation that actually happens. */
void Unit_ctor(Soldier *pUnit);

bool Unit_bIsOperative(const Soldier *pUnit);
bool Unit_bIsLarge(const Soldier *pUnit);

/* unk_138EA0: the shared empty-slot placeholder. An inventory slot can hold this
 * instead of NULL, and every inventory reader treats it as "no item". It is not a
 * real item, so it is never dropped onto the map, but the slot holding it is still
 * cleared. Modelled as a TactItem because the readers that compare against it
 * (Ui_HitTestTactItem @ 0x11ac0, Inv_HandlePointer @ 0x12260) all treat inventory
 * slots as TactItem pointers. */
extern TactItem g_itemSlotEmpty;

/* Unit_DropAllItemsToCell @ 0x4b4f8. Drop everything the unit carries into the
 * cell it stands on and clear each slot. Large units keep their hands. */
void Unit_DropAllItemsToCell(Soldier *pUnit);

/* Unit_bFacingAngleDuel @ 0x4dcf4. True when pUnitA is the better turned of the
 * two, judged from each unit's facing against the bearing to the other and then
 * settled by a reaction roll. The original's unused first context argument is
 * dropped. */
bool Unit_bFacingAngleDuel(const Soldier *pUnitA, const Soldier *pUnitB);

/* The lookup Unit_GetExtraSpr performs, split out because the splash walk calls
 * the same routine with a tile sprite record instead of a unit. Read straight off
 * the record, the two offsets are Mc1Rec.nSlot (+0x26) and Mc1Rec.bExtraSprFacing
 * (+0x32) -- the same two bytes the unit path reads at Soldier+0x26 and +0x32. */
Mc1Rec *Rec_GetExtraSpr(u16 nBaseIdx, u8 nFacingByte);

/* Unit_GetExtraSpr @ 0x2e59c. Returns the overlay sprite for the unit's
 * multi/anim state, or NULL when the low byte of the anim's uFacingResetH is
 * zero. The lookup index is g_fontSys.anMc1Base[uExtraSprBase] + that byte.
 * The original's unused first context argument is dropped. */
Mc1Rec *Unit_GetExtraSpr(const Soldier *pUnit);

/* The two object fields Unit_UpdateAnimPose reads from each hand. TactItem.pObj
 * is the minimal ObjectRec, so the caller supplies them. */
typedef struct AnimHandInfo {
    bool bPresent;
    u8   bListPri;   /* pObj->bListPri @ 0xA4 */
    u16  uSortKey;   /* pObj->uSortKey @ 0xC8 */
} AnimHandInfo;

/* Unit_UpdateAnimPose @ 0x4978c.
 *
 * Derives nAnimPose from bFlags and the two hands. Flag 0x20 (dead) forces pose
 * 18 and masks bFlags to 0xE0. Flag 4 (aiming) gives 15/16/17 by kneel(1)/climb
 * (0x10). Otherwise the walk/kneel/climb flags give a base pose 0..4 and a held
 * weapon of category bListPri 1 or sort key 22/24 raises it by 10, any other
 * hand-held item raises it by 5, and no item leaves the base pose. */
void Unit_UpdateAnimPose(Soldier *pUnit, const AnimHandInfo *pHandL,
                         const AnimHandInfo *pHandR);

/* Unit_CanStepExitDir @ 0x36d68.
 *
 * Probes up to four cells in the direction given by `nDir` (0..7) and reports
 * whether the step is clear. The probe origin is the cell's own coordinate
 * triple `MapCell.nX/nY/nZ`.
 *
 * The argument is a MapCell *, not a Soldier *. Both of the original's two code
 * callers pass `Map.pSelUnit->pCell` (ARM 0x17BEC and 0x17E08, both `LDR R1,
 * [R1,#0x10]`), and the callee then reads bytes +0x1A/+0x1B/+0x1C, which are
 * MapCell.nX/nY/nZ. Hex-Rays typed the pointer as a Soldier * and so named the
 * same three bytes `nMultiTarget`, `gap_1B` and `anim.nPathZ`; the offsets are
 * shared, the intended record is the cell. The IDB comment is corrected to match.
 *
 * Each probe entry is (dx, dy, kind); the scan stops at the first entry whose dx
 * is above 2 (the padding value). A kind-2 entry checks the cell's wall-A sprite,
 * any other kind its wall-B sprite, only when the cell passes the blank/blocking
 * test. The sprite's byte 0x2F (`gap_2f[0]`) marks a real wall; finding one makes
 * the direction blocked. Returns true when no wall was found. Cells come from the
 * injected MapCellLookup; the original indexes `apCells` directly. */
bool Unit_CanStepExitDir(const MapCell *pOrigin, i32 nDir, const MapGeom *pGeom,
                         MapCellLookup pfnLookup, void *pUser);

/* The two object fields Unit_bArmorTuWarn reads from each hand. */
typedef struct ArmorHandInfo {
    bool bPresent;
    u8   abArmorTu[3]; /* pObj->abArmorTu[3] @ 0xB3 */
    u8   bTwoHanded;   /* pObj->bTwoHanded; a two-handed weapon is ignored by a non-large unit */
} ArmorHandInfo;

/* Unit_bArmorTuWarn @ 0x4e254.
 *
 * Returns false immediately when the `byte_AC81A` option is set. Otherwise it
 * picks the armour-TU slot from `byte_AC83A` (1) / `byte_AC85A` (2), else 0, takes
 * the larger of the two hands' `abArmorTu[slot] * auStat[0] / 100` (falling back
 * for slot 0 to `abArmorTu[1]` when the slot value is 0), and warns when the unit
 * can currently just afford that many TU but spending `nTuCost` would drop it below
 * the armour cost. The warning is two status messages through the injected
 * `pfnPushStatus`: index 0 is `byte_B5F40`, index 1 the `aTextRecs[205*slot+10045]`
 * record. */
bool Unit_bArmorTuWarn(const Soldier *pUnit, const ArmorHandInfo *pHandL,
                       const ArmorHandInfo *pHandR, bool bOptAC81A, bool bOptAC83A,
                       bool bOptAC85A, i32 nTuCost,
                       void (*pfnPushStatus)(i32 nWhich, void *pUser), void *pUser);

/* Unit_SpotCellOccupant @ 0x48138.
 *
 * When the cell has an occupant and the viewer's side/status rules allow it, the
 * occupant is appended to the viewer's multi-target list and, while the world is
 * busy, to the viewer's spotted list (or, when idle, flagged through its
 * `bUnk148`). It returns false only when the cell has no occupant. The context
 * carries the four globals the original reads and writes; the focus callback
 * stands in for MapView_FocusOnUnit. */
typedef struct SpotCtx {
    bool     bBusy;           /* unk_157180->bBusy */
    bool     bQueueActive;    /* unk_157198->bQueueActive */
    Soldier *pSelUnit;        /* pCtxBase.pSelUnit */
    Soldier *pCameraUnit;     /* unk_157180->pCameraUnit, written */
    i32     *pnTrajFront1036; /* pCtxBase.nTrajFront0[1036], set to 1 */
    void    (*pfnFocusOnCell)(MapCell *pCell, void *pUser); /* MapView_FocusOnUnit */
    void    *pUser;
} SpotCtx;

bool Unit_SpotCellOccupant(Soldier *pViewer, MapCell *pCell, SpotCtx *pCtx);

/* Unit_RecalcInventoryStat @ 0x49ef0.
 *
 * Recomputes `uInvBurden`: 1 point of base burden, plus each held/backpack/belt/
 * pocket item's weight, plus the ammo object's weight for a loaded weapon whose
 * object is not unlimited (255) and not two-handed. The item object fields live on
 * the full ObjectRec, so they are injected: pfnObjWeight/pfnObjTwoHanded/
 * pfnObjAmmoCap read pItem->pObj, and pfnAmmoWeight reads apObjects[nIndex]. */
typedef struct InvStatHooks {
    u8 (*pfnObjWeight)(TactItem *pItem, void *pUser);
    u8 (*pfnObjTwoHanded)(TactItem *pItem, void *pUser);
    u8 (*pfnObjAmmoCap)(TactItem *pItem, void *pUser);
    u8 (*pfnAmmoWeight)(i32 nIndex, void *pUser);
    void *pUser;
} InvStatHooks;

u16 Unit_RecalcInventoryStat(Soldier *pUnit, const InvStatHooks *pHooks);

/* Soldier_RemoveById @ 0x36b8c.
 *
 * Two original behaviours are deliberately NOT reproduced because they are
 * memory-safety defects, not game rules:
 *   - a missing id leaves the index equal to the count, so the original frees
 *     apSoldiers[count] (one past the end) and still decrements the count.
 *     This port returns the count unchanged when nIndex is out of range.
 *   - the compaction guard `count - 1 > index` is an unsigned comparison that
 *     underflows when count == 0. This port compares the indices directly.
 * Both are listed in docs/parity/game-core.md. */
i32 Unit_RemoveAt(Soldier **apSoldiers, i32 nCount, i32 nIndex);

/* Unit_ResetBattleState @ 0x498b0. Derives TU/HP/armour from auStat:
 *   auStat[0] -> uTu, auStat[1] -> uHp, auStat[9] -> uStunOrArmor */
void Unit_ResetBattleState(Soldier *pUnit, u8 bFacing, u16 nUnitType,
                           const u16 *auStat, i32 nStatCount);

/* Unit_ApplyArmorForType @ 0x498b0 tail: types 2 and 3 get extra armour, and
 * bHumanoidInv is cleared for types <= 8. Split out because it is the only part
 * of ResetBattleState that inspects the unit type. */
i32 Unit_ArmorForType(u16 nUnitType);

/* Soldier_Generate @ 0x49410.
 *
 * Rolls a fresh soldier: identity, look, name and stats. nKind is stored to
 * nKind @ 0xB6 (0 = XCom, per the callers).
 *
 * ## The duplicate-name retry loop is dead code, and that is reproduced
 *
 * The original wraps name generation in a loop that is supposed to retry when the
 * new name collides with an existing soldier's, up to 2 attempts. The collision
 * test compiles to:
 *     ADD  R1, R1, #0xBA     ; apSoldiers[i] + 0xBA, the candidate's szName
 *     ADD  LR, R4, #0xBA     ; pSoldier + 0xBA, this soldier's szName
 *     CMP  R1, LR / BEQ ...
 * which compares two ADDRESSES inside different objects, so it can never be
 * equal. The C source was almost certainly `if (other->szName == pSoldier->szName)`
 * -- comparing array names, i.e. pointers, instead of using strcmp. So the retry
 * never fires and every soldier gets whatever name was drawn first.
 *
 * This port therefore generates the name once. Reproducing the bug faithfully
 * matters because the names in a saved game depend on the exact number of RNG
 * draws, and a "fixed" retry would consume extra draws and desynchronise the
 * stream.
 *
 * Stat rolls, in the original's exact order and ranges (all inclusive):
 *     auStat[0] = rand(10) + 50    auStat[9] = rand(30) + 40
 *     auStat[1] = rand(20) + 40    auStat[2] = rand(50) + 10
 *     auStat[3] = rand(30) + 30    auStat[4] = rand(30) + 40
 *     auStat[5] = rand(20) + 30    auStat[8] = rand(60) + 20
 *     auStat[6] = rand(20) + 10    auStat[7] = rand(100) + 1
 * and the base-stat copy is offset by one at the tail:
 *     auStatBase[0..6] = auStat[0..6]   auStatBase[7] = auStat[8]   auStatBase[8] = auStat[9]
 *
 * Returns the number of stat fields written (the original returns a loop end
 * value; callers ignore it). */
void Unit_Generate(Soldier *pUnit, u8 nKind);

/* The name-drawing half of Unit_Generate, split out because it is the part that
 * consumes the RNG in a specific order and is therefore the part worth pinning in
 * a test. Uses the extracted text tables (see pufo_boot.h). */
void Unit_GenerateName(Soldier *pUnit);

/* Unit_ApplyControlRecovery @ 0x4a444 (first half).
 *
 * Applies the knockout/recovery bookkeeping when a unit is recovered from alien
 * control. Split out from Tact_UpdateUnitControl because the rest of that
 * function moves the unit between the game's side lists, which needs the strategy
 * layer; this part is pure unit arithmetic.
 *
 * Returns false when the unit was already marked recovered, which is the
 * function's idempotence guard (`if (gap_433[1]) return;`).
 *
 * The arithmetic, in the original's order:
 *   - if uHp < bKneelOrPrimed then uHp = 0 else uHp -= bKneelOrPrimed
 *   - bUnk1037 = 0
 *   - for a SMALL unit only:
 *       if (pCell->nFlags32 & 7) != 0  -> uTuSpent += 3
 *       else if uTuSpent >= 10         -> uTuSpent -= 10
 *       else                            -> uTuSpent = 0
 *   - morale: += 5, but 100 when the old value was >= 95
 *
 * Note the TU rule has no `else` for large units: their uTuSpent is left alone.
 * Note also that the `+= 3` branch is reached when the cell has a non-zero low
 * 3-bit field, i.e. the same field GameTick_Step decrements. */
bool Unit_ApplyControlRecovery(Soldier *pUnit);

/* Facing 0..7 from a direction, Unit_FaceToward @ 0x49bf0.
 * The original uses truncated double literals, not the exact constants:
 *   subtract 1.57075   (not pi/2 == 1.5707963...)
 *   add      0.3926875 (not pi/8 == 0.3926990...)
 *   multiply 1.273277096928219 (== 8 / 6.283)
 *   wrap by 6.283
 * Reproduced exactly so the sector boundaries land where the original's do. */
#define PUFO_FACING_SUB   1.57075
#define PUFO_FACING_ADD   0.3926875
#define PUFO_FACING_SCALE 1.273277096928219
#define PUFO_FACING_WRAP  6.283
#define PUFO_FACING_COUNT 8

u8 Unit_FacingToward(i32 nFromX, i32 nFromY, i32 nToX, i32 nToY);

/* Cell lookup, shared with the map/path modules (see MapCellLookup). Alias kept
 * because the unit call sites read better with this name. */
typedef MapCellLookup UnitCellLookup;

void Unit_PlaceOnMap(Soldier *pUnit, i32 nX, i32 nY, i32 nLayer,
                     UnitCellLookup pfnLookup, void *pUser);

/* Unit_ScanVisibleUnits @ 0x4834c.
 *
 * For every unit on either roster, trace a line of sight from the viewer's cell
 * centre to the other unit's cell centre and, on success, spot that cell. The
 * centre height is `12 * nZ - ceilSpr->nObjParam + 8`, the nObjParam term applying
 * only when the cell has a ceiling sprite. Only units within 15 cells
 * (trunc(hypot) of the cell difference) are considered.
 *
 * A unit is considered when its bStatus differs from the viewer's, or when its
 * nUnitType is 16 or 17 even on the same side. Those two type ids are the only
 * evidence of what they mean; they are recorded, not interpreted.
 *
 * The original publishes the target through two globals while it traces:
 * pCtxBase.pSelUnit is replaced by the viewer and pCtxBase.anActionPhaseSave0[120]
 * by the target, and both are restored. ppScratchUnit stands in for the second.
 */
#define PUFO_SCAN_RANGE_CELLS 15
#define PUFO_UNIT_TYPE_SPOTTED_A 16
#define PUFO_UNIT_TYPE_SPOTTED_B 17

/* UnitRosters is defined in game/pufo_combat.h and LosTables in game/pufo_los.h;
 * only pointers are needed here. */
struct UnitRosters;
struct LosTables;
struct FireGate;
struct ActionCostTable;

i32 Unit_ScanVisibleUnits(Soldier *pViewer, const struct LosTables *pTables,
                          MapCellLookup pfnLookup, void *pUser,
                          SpotCtx *pSpot, const struct UnitRosters *pRosters,
                          Soldier **ppScratchUnit);

/* Unit_UpdateVisibility @ 0x48580.
 *
 * Three phases.
 *
 * 1. Vertical reveal. The unit's own cell is flagged with MapCell_SetBFlags29_4.
 *    A large unit does the same for the other three cells of its 2x2 footprint
 *    and sets their bFlags44 bit 0. Then the column below is walked from the
 *    layer under the unit down to layer 0, and a cell is revealed when the cell
 *    one layer ABOVE it has no occupant, or its occupant's byte 0x2E is 1. The
 *    column above is walked from nZ + 1 to layer 3, and a cell is revealed when
 *    its own floor sprite is absent or its byte 0x2E (bBlocksSight) is 1.
 *
 * 2. Ray fan. All 120 rays are visited and a ray is used when its aPass flag for
 *    the unit's facing is set, which is what makes the eight ray-table passes the
 *    eight facings. For a ray (dx, dy):
 *      d     = sqrt(dx*dx + dy*dy) * 16.0
 *      stepX = (16*dx + 8) / d
 *      stepY = (16*dy + 8) / d
 *      x(t)  = 16*cell.nX + 8 + t*stepX,  y(t) likewise,  t = 0 .. 239
 *    The voxel pair is truncated to ints and divided by 16 to get the cell. The
 *    sight budget starts at 240.0 and every cell whose nFlags32 low 3 bits are
 *    neither 0 nor 4 subtracts 2 * that value, floored at zero. The walk stops
 *    when t exceeds the budget, when the cell is out of bounds at nLayer, or when
 *    a wall or ceiling blocks the voxel.
 *
 *    nLayer is the cell's nZ, raised to nZ + 1 when the cell has a ceiling sprite
 *    whose signed byte 0x24 is below -10.
 *
 *    Wall A blocks when sprite byte 0x2b (bUnk2B) is non-zero and either the voxel
 *    x is exactly on a cell boundary or the loft bit is set, and then only if
 *    sprite byte 0x2a is zero or the cell's bFlags29 bit 2 is clear. Wall B is the
 *    same without the boundary case. The ceiling blocks when its loft bit is set
 *    and the mission type does not allow this ceiling id: Desert allows
 *    0x1B..0x41, Forest allows 0x1B..0x40 plus exactly 82, and every other mission
 *    type blocks. The id is the u16 at ObjectRecFull +0x28, which is bTwoHanded
 *    and bUnk41 read together.
 *
 *    Each new cell is flagged and then revealed upward and downward along the
 *    column: up from nLayer while the layer stays under 4, stopping when the cell
 *    above has a floor sprite; down from nLayer to layer 0, stopping at the first
 *    cell that has a floor sprite.
 *
 * 3. Enemy multi-target scan. For every enemy within 1..15 cells, the bearing from
 *    the viewer is compared with the facing angle. Facing 2 has its own rule
 *    (accept only between 0.785375 and 5.497625); every other facing accepts when
 *    the truncated angle error is within one facing step. On a clear trace the
 *    enemy cell gets bFlags29 bit 4 and bFlags44 bit 0, and is spotted.
 *
 * A non-operative unit skips all of it and only runs Unit_ScanVisibleUnits.
 */
#define PUFO_VIS_BUDGET      240.0
#define PUFO_VIS_MAX_STEPS   240
#define PUFO_VIS_SIGHT_RANGE 15
#define PUFO_VIS_ANGLE_MAX   5.497625   /* 7 * PUFO_FACING_STEP, the facing-2 bound */
#define PUFO_CEIL_ID_DESERT_MIN 0x1B
#define PUFO_CEIL_ID_DESERT_MAX 0x41
#define PUFO_CEIL_ID_FOREST_MIN 0x1B
#define PUFO_CEIL_ID_FOREST_MAX 0x40
#define PUFO_CEIL_ID_FOREST_ALT 82

typedef struct VisCtx {
    SpotCtx             *pSpot;      /* shared with Unit_SpotCellOccupant */
    const struct LosTables *pTables;  /* unk_15718C loft masks */
    MapCellLookup        pfnLookup;
    void                *pUser;
    const MapGeom       *pGeom;
    const struct VisRay *apRays;     /* unk_B2278, 120 records */
    const struct UnitRosters *pRosters;
    Soldier            **ppScratchUnit; /* pCtxBase.anActionPhaseSave0[120] */
    u8                   nMissionType;  /* Map.nMissionType @ 0x1511A */
    struct AimState     *pAim;          /* the Map aim block, for the arc trace */
    struct TactCtx272   *pTactCtx;      /* apOppFire / nOppFireCount */
    u8                  *pbFireStatus1; /* Map.bFireStatus0[1] @ 0x51 */
} VisCtx;

i32 Unit_UpdateVisibility(Soldier *pUnit, VisCtx *pCtx);

/* UnitList_ForEachRefresh @ 0x22b54. Refresh visibility for every Xcom unit.
 * Called at the end of Tact_KillUnit when the death message path is armed. */
void UnitList_ForEachRefresh(struct UnitRosters *pRosters, VisCtx *pCtx);

/* --- Tact_KillUnit @ 0x4e430 context ----------------------------------------
 *
 * KillCtx carries what the kill path reaches through pCtxBase. The combat fields are
 * a pointer to the existing CombatCtx rather than a copy of them.
 *
 * pszTransformName is Map + 0x230, the NUL-terminated name the chryssalid transform
 * copies into Soldier.szName. pnKillCount is the dword at Map.gap_TrajRest + 4*49940.
 * bTickHold is Map.bGameTickHold (0x5A) and bFrameScratch is byte_138E74, the frame
 * scratch GameTick_OnTimer clears; either one suppresses the death message.
 * bUiSlotArmed is the low byte of Map.apUiSlotsA[2], which arms the spotted-flag
 * sweep and the Xcom visibility refresh.
 *
 * The five message tails are text records the original reaches by raw index into
 * unk_B38D0: 3075 (alive male), unk_B45A0 (alive female), 8610 (dead female, type
 * < 4), 8405 (dead male, type < 4) and 218735 (dead, type 4..8). The port has no raw
 * text-record accessor yet, so the strings are supplied. */
/* Forward declarations only. pufo_combat.h and pufo_statusmsg.h both pull this
 * header in, so their full definitions are not reachable from here; pufo_unit.c
 * includes them. */
typedef struct CombatCtx CombatCtx;
typedef struct ShotHooks ShotHooks;
typedef struct StatusStack StatusStack;
typedef struct StatusHooks StatusHooks;

typedef struct KillCtx {
    CombatCtx          *pCombat;        /* lookup, geom, rosters' shot origin */
    struct UnitRosters *pRosters;
    TactItem          **apTactItems;    /* Map.apTactItems */
    i32                *pnTactItem;     /* Map.nTactItemCount, in/out */
    ObjectRecFull     **apObjects;      /* Map.apObjects */
    Soldier           **ppSelUnit;      /* Map.pSelUnit */
    i32                *pnKillCount;    /* Map.gap_TrajRest + 4*49940 */
    const char         *pszTransformName;
    bool                bTickHold;      /* Map+0x0012 unk_113A12: skips the message
                                         * and the sweep tail (0x4F0AC) */
    bool                bFrameScratch;  /* Map+0x254BC unk_138E74: same early exit */
    bool                bUiSlotArmed;   /* Map+0x0020 unk_113A20: arms the spotted-flag
                                         * sweep and the Xcom refresh (0x4F24C) */
    StatusStack        *pStatus;
    const StatusHooks  *pStatusHooks;
    VisCtx             *pVisCtx;
    const char         *pszMsgAliveMale;
    const char         *pszMsgAliveFemale;
    const char         *pszMsgDeadFemale;
    const char         *pszMsgDeadMaleSmall;
    const char         *pszMsgDeadMaleBig;
} KillCtx;

/* pfnCheckBattleEnd stands for Tact_CheckBattleEnd @ 0x34fbc. That function is
 * mostly debrief UI (UiControl / UiPanel construction) and belongs to the ui-input
 * subsystem, so the seam is explicit rather than a stub. pShotHooks is what
 * Combat_ResolveShot needs for the type-18 explosion. */
typedef struct KillHooks {
    void (*pfnCheckBattleEnd)(void *pUser);
    const ShotHooks *pShotHooks;
    void *pUser;
} KillHooks;

void Tact_KillUnit(const KillCtx *pCtx, const KillHooks *pHooks, Soldier *pUnit);


/* Unit_bTryOpportunityFire @ 0x47c38. Offer one opportunity shot. See the
 * comment on the definition for the gate chain and for the quirk that a target
 * which turns out not to be spotted still reports success. */
bool Unit_bTryOpportunityFire(Soldier *pTarget, Soldier *pShooter,
                              const struct FireGate *pGate, TactItem **ppSelWeapon,
                              i32 *pnActionMode, struct ActionCostTable *pCosts,
                              VisCtx *pCtx);

#endif /* PUFO_UNIT_H */
