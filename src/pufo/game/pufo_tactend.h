/* pufo_tactend.h - tearing down one tactical mission.
 *
 * Evidence: Tact_Shutdown @ 0x3d568 (3,788 bytes).
 *
 * The original takes one byte. Non-zero keeps the geoscape world and drops only the
 * tactical UI plus the tactical item pool; zero also commits the mission outcome and
 * removes the tactical units. Both paths then free the same tactical map and sprite
 * state, so the split is one branch in the middle, not two separate functions.
 *
 * ## The bank pad walk
 *
 * Thirteen groups, each freeing three adjacent pad pointers per widget. Hex-Rays
 * renders the inner step as `v12 = (UiControl *)((char *)v12 + 4` and the group base
 * as `v17[19]`, which reads as four WIDGETS apart. It is not: the step is four BYTES,
 * so the three pointers are one widget's pPad0/pPad1/pPad2 (UiControl + 0x14, +0x18,
 * +0x1C) and the group base is a widget index. The zeroing writes back into the same
 * three slots -- `*((_DWORD *)&unk_AC930 + 8*j + v16 + 5)` is unk_AC930 + 32*j +
 * 4*(v16+5), and unk_AC930 is pBank + 0x260 = pBank + 32*19, so dword 5 is +0x14.
 *
 * The thirteen groups cover bank widgets 0..112 and 128..129. StartUi_Shutdown
 * (0x38d7c) covers 113..127. Together they cover all 130 exactly once, which is the
 * cross-check that the group table below is right.
 *
 * ## Deviations
 *
 * 1. The original operator-deletes a global and leaves the global dangling. The port
 *    takes every freed slot as a pointer-to-pointer and clears it, because a second
 *    shutdown would otherwise be a double free. This is the same deviation already
 *    recorded for UiControl_FreePads @ 0x1a2c8.
 * 2. `UiPanel_FreeLines` (0x3e8c8) and `UiControl_FreePads` (0x1a2c8) live in
 *    pufo_ui, and pufo_ui links pufo_game. They are reached through hooks so this
 *    module does not close the cycle. The hooks are load-bearing: without them
 *    pufo_game would depend on pufo_ui. See the separate issue in PLAN.md.
 */
#ifndef PUFO_TACTEND_H
#define PUFO_TACTEND_H

#include "game/pufo_font.h"
#include "game/pufo_geoscape.h"
#include "game/pufo_mapcell.h"
#include "game/pufo_rmp.h"
#include "game/pufo_save.h"    /* SmapSite, the record Tact_TallyCraftRecovery parks loot into */
#include "game/pufo_ship.h"
#include "game/pufo_tactctl.h" /* PUFO_MISSION_TYPE_XCOM_BASE / _ALIEN_BASE */
#include "pufo_common.h"
#include "ui/pufo_control.h"
#include "ui/pufo_panel.h"

struct PtrArray;
struct Soldier;

/* The four message panels get nLineCount = 14 before they are freed; the fifth gets 28. */
#define PUFO_SHUTDOWN_MSG_PANELS   4
#define PUFO_SHUTDOWN_LINE_COUNT   14
#define PUFO_SHUTDOWN_MSG_PANEL_28 28

/* Pad globals freed before the bank walk, and after it. Order is the original's. */
#define PUFO_SHUTDOWN_PAD_EARLY 2   /* 0x15692C, 0x156928 */
#define PUFO_SHUTDOWN_PAD_LATE  7   /* 0x113AB8, 0x1571F0, 0x156A70, 0x156AA0,
                                     * 0x156A74, 0x128AC0, 0x113AB4 */
#define PUFO_SHUTDOWN_PAD_TAIL  3   /* 0x113ABC, 0x138EB8, 0x156A80 */

/* The tactical sprite set at Map+0x199C, freed as 4 x 2 x 4, and the eleven slots at
 * Map+0xCC. */
#define PUFO_SHUTDOWN_SPRITE_SET 32
#define PUFO_SHUTDOWN_MAP_PADS   11

/* One bank pad group: the first widget and how many widgets it covers. */
typedef struct TactShutdownBankGroup {
    u8 nFirst;
    u8 nCount;
} TactShutdownBankGroup;

#define PUFO_SHUTDOWN_BANK_GROUPS 13
extern const TactShutdownBankGroup g_aTactShutdownBankGroups[PUFO_SHUTDOWN_BANK_GROUPS];

/* Seams. The first two stand in for the audio and timer shutdowns, which need a
 * backend the caller owns -- the same seam StartUi_ShutdownHooks already uses. The
 * last two stand in for the pufo_ui callees, so pufo_game does not depend on pufo_ui. */
typedef struct TactShutdownHooks {
    void (*pfnSoundShutdown)(void *pUser);                    /* Sound_Shutdown (0x9e3d4) */
    void (*pfnKillTimers)(void *pUser);                       /* KillTimers_BySpeed (0x9e8b4) */
    void (*pfnPanelFreeLines)(UiPanel *pPanel, void *pUser);  /* UiPanel_FreeLines (0x3e8c8) */
    void (*pfnControlFreePads)(UiControl *pCtl, void *pUser); /* UiControl_FreePads (0x1a2c8) */
    void *pUser;
} TactShutdownHooks;

typedef struct TactShutdownEnv {
    /* --- the tactical UI --- */
    /* Every slot below is a pointer to the caller's storage, not a pointer held in the
     * env: the env is const, and the port clears each freed slot through it. */
    UiPanel  **appMsgPanel;      /* 4 entries: 0x1568DC, 0x1568E0, 0x1568E4, 0x1568E8 */
    UiPanel  **appLinePanel;     /* unk_1571EC, nLineCount set to 28 */
    UiControl **appLookCtl;      /* dword_1571E4 */
    UiControl  *pBank;           /* pBank @ 0xAC6D0 */

    PadImage **appPadEarly;      /* PUFO_SHUTDOWN_PAD_EARLY entries */
    PadImage **appPadLate;       /* PUFO_SHUTDOWN_PAD_LATE entries */
    PadImage **appPadTail;       /* PUFO_SHUTDOWN_PAD_TAIL entries */
    PadImage **appSpriteSet;     /* 32 slots at Map+0x199C */
    PadImage **appMapPads;       /* 11 slots at Map+0xCC */

    RmpData  **appRmp;           /* pRmpMissionData @ 0x15881C */
    void     **appBlock156A78;   /* deleted with no null test */
    void     **appBlock187050;   /* deleted only on the bKeepSome path */
    PufoFont  *pFont;            /* fontSys @ 0x158268 */
    u8        *pbActionLatch;    /* Map+0x00, cleared first */

    /* --- the tactical item pool, freed on both paths --- */
    TactItem **apTactItems;      /* Map+0x23438 */
    i32       *pnTactItemCount;  /* Map_nTactItemCount @ Map+0x1513C */

    /* --- the bKeepSome path: the geoscape rosters stay, their tactical copies go --- */
    void **apStrategCraftCopy;   /* Map+0x1198, count unk_115350 (u16) */
    u16   *pnStrategCraftCopy;
    void **apBaseCopy;           /* Map+0x1174, count is the signed byte at 0x159D80 */
    i8    *pnBaseCount;
    void **apCraftCopy;          /* Map+0x10AC, count Map_nCraftCount */
    u8    *pnCraftCount;
    struct PtrArray *pStrategList;

    /* --- the full-shutdown path: tactical units --- */
    struct Soldier **apCivilianUnits;  /* Map+0x15244 */
    i32             *pnCivilianCount;  /* Map+0x15120 */
    struct Soldier **apEnemyUnits;     /* Map+0x151CC */
    u8              *pnEnemyCount;     /* Map+0x15124, a byte in the original */
    struct Soldier **apXcomUnits;      /* Map+0x15154 */
    u8              *pnXcomCount;      /* Map+0x1511F, a byte in the original */

    /* --- the mission outcome decision table --- */
    u8 *pbMissionMode;           /* Map_bMissionMode @ Map+0x1511D */
    u8 *pbMissionAborted;        /* unk_128A8C @ Map+0x150D4 */
    u8 *pbAlternateOutcomePath;  /* unk_1153D9 */
    u8 *pbFacilityType;          /* unk_159D81, index into the base's facility grid */
    i32 *pnStrategIndex;         /* MEMORY[0x159E74], the current strateg item */
    u8 *pnCraftIndex;            /* Map_nCraftIndex @ Map+0x15113 */
    u8 *pbSpawnOnceFlag;         /* 0x162028, the replacement-spawn gate */
    const BaseDemolishEnv   *pDemolishEnv;
    const ShipRemovalEnv    *pShipEnv;
    const TacticalLootEnv   *pLootEnv;

    /* --- the tactical map itself --- */
    MapCell **apCells;      /* Map_apCells0 @ Map+0x15338, 3600 slots per layer */
    u8      *pnMapDimX;     /* Map_nMapDimX @ Map+0x1511B, in map tiles */
    u8      *pnMapDimY;     /* Map_nMapDimY @ Map+0x1511C, in map tiles */
    void    **appLoftemps;  /* unk_15718C */
    void    **appUnitloft;  /* unk_1571E8 */
    void    **appMapTactical; /* pMapTactical @ 0x157198 */
    void    **appMapCtx;    /* unk_157180 */

    TactShutdownHooks hooks;
} TactShutdownEnv;

/* Tact_Shutdown @ 0x3d568. bKeepSome non-zero keeps the world and the units and drops
 * only the tactical UI, the tactical item pool and the tactical map. */
void Tact_Shutdown(const TactShutdownEnv *pEnv, u8 bKeepSome);

/* ============================================================================== the four tallies
 *
 * Evidence: Tact_TallyBattleStats @ 0x3348c (756 B), Tact_TallyUnitResults @ 0x33780 (1,136 B),
 * Tact_TallyItemRecovery @ 0x33c80 (2,100 B), Tact_TallyCraftRecovery @ 0x344b4 (556 B). All four
 * run back to back inside their caller (Tact_CheckBattleEnd, 0x34fbc, not ported yet): 0x35240,
 * 0x35284, 0x352b0, 0x352bc.
 *
 * ## What the tallies are for
 *
 * Tact_CheckBattleEnd turns their outputs into the mission debrief. Each counter it prints is
 * labelled with a TEXTS record, and the ARM builds every one of those addresses as
 * `LDR R0, =0xB38D0` plus `MOV R3, #hi; ORR R3, #lo` -- aTextRecs + 205*n, the same stride
 * PUFO_TEXT_REC_STRIDE records:
 *
 *   0x35428 pnSurvived -> TEXTS[162] "Operatives killed"   0x35500 pnKilled   -> 163 "Tanks Destroyed"
 *   0x355c4 pnMIA      -> TEXTS[164] "Civilians Killed"    0x35688 pnCivOk    -> 165 "Civilians Rescued"
 *   0x3575c pnCivDead  -> TEXTS[166] "Aliens Killed"
 *   0x35b40 pnH  -> 171 "Alien Alloys"       0x35c6c pnB -> 172 "Elerium 115"
 *   0x35d70 pnD  -> 173 "UFO Navigation"     0x35e80 pnA -> 174 "UFO Power Source"
 *   0x35f90 pnC  -> 175 "Alien Reproduction" 0x36090 pnE -> 176 "Alien Food"
 *   0x36298 pnI  -> 177 "Alien Entertainment"
 *   0x363a0 pnG  -> 178 "Alien Surgery"      0x3618c pnJ -> 198 "Examination Room"
 *
 * So the first three out-params of Tact_TallyUnitResults are loss lines, not survival lines: the
 * IDB's pnSurvived / pnKilled / pnMIA names describe the type ranges, not the debrief meaning, and
 * the port names them after the debrief line each one feeds.
 *
 * ## The tile facility byte, Mc1Rec+0x1E
 *
 * Tact_TallyBattleStats walks four layers x (10 * nMapDimY) rows x (10 * nMapDimX) columns and, per
 * cell, the four sprite slots in the order pFloorSpr, pWallSprA, pWallSprB, pCeilSpr (ARM 0x335C8:
 * v14 == 0 takes the floor, switch cases 1..3 the other three). The byte at Mc1Rec+0x1E names the
 * alien-base facility that sprite contributes to. Two independent sides give the names: the debrief
 * line each counter feeds (above), and which shipped .MC1 sets carry the value at all (main.dat,
 * 52-byte records, 1,163 records over 27 sets):
 *
 *   2   1 record,  U_WALL02                          -- the Elerium container
 *   3   6 records, U_WALL02                          -- navigation
 *   5   4 records, U_DISEC2                          -- reproduction
 *   7   4 records, U_PODS                            -- entertainment
 *   8   6 records, U_DISEC2                          -- surgery
 *   9   4 records, U_OPER2                           -- examination
 *   10  133 records: U_BASE 66, U_EXT02 26, U_WALL02 19, UFO1 14, U_OPER2 6, MARS 2 -- alloys
 *
 * Values 0 (911 records: every natural, craft and furniture set), 1 (109: AVENGER, LIGHTNIN, PLANE,
 * XBASE1), 12 (49), 13 (MARS) and 14 (BRAIN) are never tallied. Only the value-2 counter also adds
 * an amount: the Elerium total grows by unk_138E6C (Map+0x254B4, the rand(0x28) + 10 roll
 * Tact_InitMission seeds). The food counter is not a tile count at all -- the only write is the
 * tail, from word_138E70 (Map+0x254B8, rand(0x14)), and only when Map_nTerrainVariant > 3 or the
 * mission type is the alien base (ARM 0x3373C..0x33768, CMP #3 BHI / CMP #4). The sixth counter is
 * dead: no tile value writes it and no reader reads it.
 *
 * The gate at 0x334B4 is `Map_bAltOutcomePath != 0 || unk_128A8C == 0`, so the walk runs only when
 * unk_128A8C is set and the alternate outcome path is not.
 *
 * ## The open question on unk_128A8C
 *
 * The port calls this byte pbMissionAborted, "the mission ended in a retreat rather than a completed
 * landing" (docs/parity/game-core.md). The tally's counting rule reads more naturally the other way
 * round: a unit counts as lost when `uHp == 0`, or when the byte is CLEAR and the alternate path is
 * clear, or when the alternate path is set and the unit is outside the camera box -- so with the
 * byte clear every operative and every tank lands on the "killed"/"destroyed" line, and civilians
 * count as rescued only when the byte is set. That is consistent with the byte meaning "the team was
 * extracted", which is the opposite polarity. No writer of the byte is visible in the IDB (its eight
 * xrefs are all reads, in TactHud_DrawStatusBanner and Tact_Shutdown), so the name is left as it is
 * and the question is recorded in PLAN.md for the Tact_CheckBattleEnd unit.
 */

/* The debrief counters Tact_TallyBattleStats fills, named after the line each one feeds. */
typedef struct TactBattleStats {
    i32 nPowerSource;   /* tile facility 2, one per sprite */
    i32 nElerium;       /* tile facility 2, the seeded Elerium amount added per sprite */
    i32 nReproduction;  /* tile facility 5 */
    i32 nNavigation;    /* tile facility 3 */
    i32 nFood;          /* the mission tail only, never a tile count */
    i32 nUnused;        /* the original's sixth counter: no writer, no reader */
    i32 nSurgery;       /* tile facility 8 */
    i32 nAlloys;        /* tile facility 10 */
    i32 nEntertainment; /* tile facility 7 */
    i32 nExamination;   /* tile facility 9 */
} TactBattleStats;

typedef struct TactTileTallyEnv {
    MapCell *const *apCells;    /* apCells0 @ Map+0x15338, 4 layers x 3600 slots */
    const u8  *pnMapDimX;       /* Map_nMapDimX @ Map+0x1511B, in tiles */
    const u8  *pnMapDimY;       /* Map_nMapDimY @ Map+0x1511C, in tiles */
    const u8  *pbAltOutcomePath;/* Map_bAltOutcomePath @ Map+0x1A21 */
    const u8  *pbMissionAborted;/* unk_128A8C @ Map+0x150D4, see the open question above */
    const u8  *pnTerrainVariant;/* Map_nTerrainVariant @ Map+0x15111 */
    const u8  *pnMissionType;   /* Map_nMissionType @ Map+0x1511A */
    const i32 *pnEleriumRoll;   /* unk_138E6C @ Map+0x254B4 */
    const i32 *pnFoodRoll;      /* unk_138E70 @ Map+0x254B8 */
} TactTileTallyEnv;

/* Tact_TallyBattleStats @ 0x3348c. All ten counters are zeroed first, whatever the gate decides. */
void Tact_TallyBattleStats(const TactTileTallyEnv *pEnv, TactBattleStats *pStats);

/* ---------------------------------------------------------------------------- unit losses and zone */

/* The five debrief lines Tact_TallyUnitResults fills. */
typedef struct TactUnitResults {
    i32 nOperativesLost;   /* nUnitType 0..3,   debrief TEXTS[162] */
    i32 nTanksLost;        /* nUnitType 4..8,   debrief TEXTS[163] */
    i32 nCiviliansKilled;  /* nUnitType 0x10/11 not rescued, TEXTS[164] */
    i32 nCiviliansRescued; /* nUnitType 0x10/11 rescued,     TEXTS[165] */
    i32 nAliensKilled;     /* nUnitType 9..0xF, 0x12..0x15 with uHp 0, TEXTS[166] */
} TactUnitResults;

typedef struct TactUnitTallyEnv {
    Soldier **apSoldiers;       /* apSoldiers @ Map+0x1198 */
    const u16 *pnSoldierCount;  /* nSoldierCount @ 0x115350 */
    Soldier **apXcomUnits;      /* apXcomUnits @ Map+0x15154 */
    const u8  *pnXcomCount;     /* nXcomUnitCount @ Map+0x1511F */
    Soldier **apEnemyUnits;     /* apEnemyUnits @ Map+0x151CC */
    const u8  *pnEnemyCount;    /* nEnemyUnitCount @ Map+0x15124 */
    Soldier **apCivilianUnits;  /* apCivilianUnits @ Map+0x15244 */
    const i32 *pnCivilianCount; /* nCivilianUnitCount @ Map+0x15120 */
    const u8  *pbMissionAborted;
    const u8  *pbAltOutcomePath;
} TactUnitTallyEnv;

/* Tact_TallyUnitResults @ 0x33780. Clears Soldier.bInZone for every soldier, marks the Xcom and
 * civilian units inside the box, counts their missions, then classifies all three rosters. */
void Tact_TallyUnitResults(const TactUnitTallyEnv *pEnv, i32 nX0, i32 nY0, i32 nX1, i32 nY1,
                           TactUnitResults *pRes);

/* ------------------------------------------------------------------ items dropped into the cells */

typedef struct TactItemTallyEnv {
    MapCell *const *apCells;    /* apCells0 @ Map+0x15338 */
    ObjectRec **apObjects;      /* apObjects @ Map+0x108 */
    const i32 *pnObjectCount;   /* Map_nObjectCount @ Map+0x10A8 */
    Ship **apCraft;             /* apCraft @ Map+0x10AC */
    const u8  *pnCraftIndex;    /* Map_nCraftIndex @ Map+0x15113 */
    Soldier **apEnemyUnits;     /* Map+0x151CC */
    const u8  *pnEnemyCount;    /* Map+0x15124 */
    Soldier **apXcomUnits;      /* Map+0x15154 */
    const u8  *pnXcomCount;     /* Map+0x1511F */
    Soldier **apCivilianUnits;  /* Map+0x15244 */
    const i32 *pnCivilianCount; /* Map+0x15120 */
    const u8  *pbMissionAborted;
    const u8  *pbAltOutcomePath;
} TactItemTallyEnv;

/* Tact_TallyItemRecovery @ 0x33c80. Empties every carried item onto its cell, tallies what lies in
 * the box into ObjectRec.uRecovCount, credits each live alien inside the box with its race/rank
 * object, and credits each live HWP inside the box with its weapon plus its clip into the craft
 * cargo. The original's return value is a leftover register the caller overwrites, so the port
 * returns nothing. */
void Tact_TallyItemRecovery(const TactItemTallyEnv *pEnv, i32 nX0, i32 nY0, i32 nX1, i32 nY1,
                            i32 *pnScore);

/* ------------------------------------------------------------- capping recovery against the cargo */

typedef struct TactCraftTallyEnv {
    ObjectRec **apObjects;      /* apObjects @ Map+0x108 */
    const i32 *pnObjectCount;   /* Map_nObjectCount @ Map+0x10A8 */
    Ship **apCraft;             /* apCraft @ Map+0x10AC */
    const i32 *pnCraftCount;    /* Map_nCraftCount @ Map+0x1194 */
    const u8  *pnCraftIndex;    /* Map_nCraftIndex @ Map+0x15113 */
    const u8  *pnMissionType;   /* Map_nMissionType @ Map+0x1511A */
    const i8  *pnHomeBase;      /* nCraftHomeBase @ 0x158848, the base being defended */
    SmapSite *aSites;           /* aSmapSites @ 0x162030 */
    const i32 *pnSiteCount;     /* nSmapSiteCount @ 0x186410, the working site slot */
    const u8  *pbMissionAborted;
    const u8  *pbAltOutcomePath;
} TactCraftTallyEnv;

/* Tact_TallyCraftRecovery @ 0x344b4. Caps each object's uRecovCount against the per-craft quantity
 * slots of the craft the mission deployed, then parks what is left either in the defended base's
 * per-base quantity or in the working site record. *pbLootRecorded is set to 1 exactly when a
 * non-zero remainder reached the site record. */
void Tact_TallyCraftRecovery(const TactCraftTallyEnv *pEnv, i32 *pbLootRecorded);

/* The craft index list Tact_TallyCraftRecovery builds for an Xcom base defence. The original keeps
 * it in a 20-dword stack array and appends without bounds, so more than 20 home-base matches would
 * walk off the frame; the port stops at the capacity and records the difference. */
#define PUFO_TALLY_CRAFT_LIST_MAX 20

#endif /* PUFO_TACTEND_H */
