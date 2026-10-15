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
    u8 *pbMissionVictory;        /* Map_bMissionVictory @ Map+0x150D4 */
    u8 *pbAlternateOutcomePath;  /* Map_bAltOutcomePath @ 0x1153D9 */
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
 * The gate at 0x334B4 is `Map_bAltOutcomePath != 0 || Map_bMissionVictory == 0`, so the walk runs
 * only when the mission was won and not aborted.
 *
 * ## Map_bMissionVictory @ Map+0x150D4 (was unk_128A8C)
 *
 * The port first called this byte pbMissionVictory, and that polarity was wrong. Tact_CheckBattleEnd
 * writes it, and the writes settle it:
 *
 *   0x350F0  set when no enemy is left (Map_nEnemyReserveCount plus the live non-civilian enemies
 *            counted off apEnemyUnits is zero, and it is not an unfinished alien base)
 *   0x35070  cleared when Map_nXcomUnitCount is zero
 *   0x351CC  cleared when the mission is aborted and Map_nMissionType is MissionType_XcomBase
 *
 * and the debrief reads it as the win/lose switch: 0x353C4 copies TEXTS[160] "Victory" into
 * Map_szMissionCaption @ Map+0x15094 when it is set, 0x35400 copies TEXTS[161] "Defeat" when it is
 * clear, and TactHud_DrawStatusBanner (0x1E968) draws that caption green when set, red when clear.
 * The IDB's eight data xrefs are all reads because every write here goes through a register-built
 * offset (`MOV R8,#0x15000; ORR R8,R8,#0xD4; STRB R7,[R5,R8]`), which is why no writer looked to
 * exist. The companion dword Map_nMissionOutcome @ Map+0x1A1C gets 1 (no Xcom left), 0 (no enemies
 * left) or 2 (aborted) and has no reader anywhere in the binary.
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
    const u8  *pbMissionVictory;/* Map_bMissionVictory @ Map+0x150D4, see the note above */
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
    const u8  *pbMissionVictory;
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
    const u8  *pbMissionVictory;
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
    const u8  *pbMissionVictory;
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

/* ================================================================== the post-mission stat refresh
 *
 * Evidence: Tact_ClearSoldierBattleFlags @ 0x346e0 (2,268 B).
 *
 * Five steps, in this order:
 *
 *   1. clear Soldier.bRankDirty over apSoldiers (0x34718)
 *   2. Soldier_PromoteByTypeScores (0x47280) runs the rank ladder and sets bRankDirty on
 *      whoever climbed
 *   3. for every soldier with bInZone set and Unit_bIsOperative true, recompute eight stats
 *      from g_aClassStatGrowth[Soldier.nClass] (0x347A8..0x34E10)
 *   4. for each of those same soldiers that also has bRankDirty set, append one row to the
 *      promotion report: Soldier.szName into the name panel and the rank name into the rank
 *      panel, both at apLines[n], then bump both panels' nLineCount and set
 *      Map_bPromotionReport (0x34E10..0x34EDC)
 *   5. clear bRankDirty again (0x34F90)
 *
 * ## The stat refresh
 *
 *     auStat[k] = (u16)((double)g_aClassStatGrowth[9 * nClass + j] * dRate * (double)uMissions
 *                       + (double)auStatBase[b])
 *
 * with these (class column j, auStat index k, auStatBase index b, rate) quadruples:
 * in the order the ARM emits them. The rates are the literal doubles at 0x347D8..0x34D98; the original writes them as
 * two dword halves, e.g. `LDR R2,=0x22222222; LDR R3,=0x3FEA2222`.
 *
 *   j  k  b  rate                    auStat meaning (Unit_ResetBattleState @ 0x498b0)
 *   0  0  0  0.8166666666666667      auStat[0] -> uTu
 *   8  9  8  0.31666666666666665     auStat[9] -> uStunOrArmor
 *   1  1  1  0.4                     auStat[1] -> uHp
 *   2  2  2  0.3833333333333333
 *   3  3  3  0.35
 *   4  4  4  0.31666666666666665
 *   5  5  5  0.2833333333333333
 *   7  8  7  0.4
 *
 * Class column 6 is never read, and auStat[6] and auStat[7] are never recomputed -- the two
 * stats the classification averages call "C" keep whatever they had. b is the stat's own base
 * slot, not a copy of k: the two rules that grow a compacted stat read the compacted slot, which
 * is the mapping Soldier_GenerateRecords records (auStatBase[7] = auStat[8], auStatBase[8] =
 * auStat[9]). The ARM spells it out in the operand offsets -- the j=8 rule reads
 * `auStatBase+0x10` (index 8) and stores `auStat+0x12` (index 9), and the j=7 rule reads `0xF2`
 * (index 7) and stores `0xB2` (index 8). auStatBase holds nine entries, so reading index 9 would
 * run off the array. The double product is converted with
 * _dtou, i.e. truncated toward zero, and then stored with STRH.
 *
 * ## The promotion report panels
 *
 * The name panel is `pUiPanel2 @ 0x1568E0` and the rank panel is `pUiPanel3 @ 0x1568DC`; both
 * line buffers are 50 bytes (PUFO_PANEL_LINE_BYTES) and the original walks them with a byte
 * cursor that advances 4 per row, which is just `apLines[n]`. Tact_CheckBattleEnd zeroes both
 * panels' nLineCount at 0x352E0 / 0x352F0 before calling this, and Map_bPromotionReport is what
 * UiScreen_OnCancel (0x162B8) later uses to send the debrief to screen 19 instead of shutting
 * the mission down.
 */
#define PUFO_CLASS_STAT_COUNT      9  /* g_aClassStatGrowth stride per class */
#define PUFO_SOLDIER_CLASS_COUNT   6  /* Unit_ClassFromStats yields 0..5 */

/* g_aClassStatGrowth @ 0xAA930 (was byte_AA930): six classes x nine growth weights, values 1..3.
 * Tact_ClearSoldierBattleFlags is its only reader in the binary. Byte-checked by
 * docs/parity/audit_tables.py. */
extern const u8 g_aClassStatGrowth[PUFO_SOLDIER_CLASS_COUNT * PUFO_CLASS_STAT_COUNT];

typedef struct TactBattleFlagsEnv {
    Soldier **apSoldiers;        /* Map_apSoldiers @ Map+0x1198 */
    const u16 *pnSoldierCount;   /* nSoldierCount @ 0x115350 */
    UiPanel *pPanelName;         /* pUiPanel2 @ 0x1568E0, the promotion report's name column */
    UiPanel *pPanelRank;         /* pUiPanel3 @ 0x1568DC, its rank column */
    const char *panRankName;     /* aszTextE @ 0xB2200, six 20-byte Xcom rank names */
    u8   *pbPromotionReport;     /* Map_bPromotionReport @ 0x1153D8 */
} TactBattleFlagsEnv;

/* Tact_ClearSoldierBattleFlags @ 0x346e0. */
void Tact_ClearSoldierBattleFlags(const TactBattleFlagsEnv *pEnv);

/* ================================================================== the battle-end gate and the debrief
 *
 * Evidence: Tact_CheckBattleEnd @ 0x34fbc (5,748 B, 122 blocks).
 *
 * ## The head: three early exits, then the win/lose decision
 *
 * `bAbort` is stored straight into Map_bAltOutcomePath (0x34FE4) -- the caller's abort request IS
 * the alternate-outcome path the tallies gate on. Then, in order:
 *
 *   Map_bBattleEnded != 0                    -> return, nothing to do (0x34FEC)
 *   Tact_ScanVictoryTiles() != 0             -> Tact_Shutdown(1) + EndScreen_Init(1), tick hold,
 *                                              battle ended, return (0x35004..0x35030)
 *   bAbort != 0                              -> Map_nMissionOutcome = 2, tick hold (0x35104)
 *   Map_nXcomUnitCount == 0                  -> Map_bMissionVictory = 0, tick hold, outcome 1
 *   live non-civilian enemies + reserve == 0 -> Map_bMissionVictory = 1, outcome 0, tick hold
 *     and not an unfinished alien base          (0x350F0). The enemy count walks apEnemyUnits @
 *                                              Map+0x151CC and skips nUnitType 0x10/0x11, the
 *                                              civilians (0x350A0, 0x350A4).
 *   Map_bTickHold == 0 after all that        -> return: the battle is still running (0x35118)
 *
 * Map_nMissionOutcome @ Map+0x1A1C gets 1 / 0 / 2 for lost / won / aborted and has no reader
 * anywhere in the binary -- a dead store the port reproduces.
 *
 * ## The fourth exit: a mission that cannot be completed
 *
 *   0x35124  (AlienBase || MarsOrSpecial) && Map_bSpawnAlt != 0
 *   0x35150  abFacilityGrid[0x618] == 1 && nMissionType == XcomBase
 *
 * either one, together with Map_bMissionVictory == 0, gives Tact_Shutdown(1) + EndScreen_Init(0)
 * and returns (0x3517C..0x35198). The second arm is reached only when the type is neither 4 nor 9,
 * which is what makes its `== 3` test the failed base-defence path. abFacilityGrid+0x618 is read as
 * a SIGNED byte (LDRSB at 0x35160) and compared to 1.
 *
 * ## The debrief setup
 *
 * Tact_GetCameraBounds (0x3519C) gives the box the unit and item tallies use. An aborted
 * Xcom-base mission clears Map_bMissionVictory AND the local copy of bAbort (0x351CC, 0x351D0),
 * which is why the TEXTS[168] "Craft Lost" line is not emitted for a base defence. Then
 * Map_nUiScreen = 13 (0x351EC, a STRH), Map_bBattleEnded = 1, dword_113A1C = 10 (the banner fade
 * counter TactHud_DrawStatusBanner gates on), pTactCtx272->bBusy = 0 (0x35228) and
 * Map_bEnemyTurnActive = 0 (0x3523C).
 *
 * The four tallies run back to back, then Map_bPromotionReport = 0 and both promotion panels'
 * nLineCount = 0, then every soldier is marked lost -- Soldier.bFlags |= 0x20 -- when
 *
 *   (aborted and the soldier is outside the extraction box) or (not aborted and the mission was lost)
 *
 * (0x3531C..0x35364). Bit 5 of bFlags is the dead bit Unit_AnimatePose reads, so a wipe or an
 * abandoned squad is killed off here rather than removed. Tact_ClearSoldierBattleFlags (0x35380)
 * then runs the rank ladder and the stat refresh.
 *
 * ## The debrief lines
 *
 * Map_nDebriefLine @ Map+0x150D0 is the line cursor. Each line copies its TEXTS record into
 * pUiPanel0 @ 0x1568E8 apLines[n] (the label column), colours both columns with
 * UiPanel_SetLineMeta, and writes the value into pUiPanel1 @ 0x1568E4 anLineAux[n]. Three meta
 * shapes appear: red (255, 0, 0, 90, 0, 0), green (0, 255, 0, 0, 90, 0) and -- only on the nine
 * loot lines -- (0, 255, 255, 0, 90, 90). The running score is the sum of the emitted terms; it
 * lands in g_nFundPending (0x365B0) and, as text, in Map_szMissionScore @ Map+0x150B2 (0x3654C).
 *
 *   gate                                      TEXTS  value column / score term
 *   operatives lost > 0                       162    -30 each
 *   tanks lost > 0                            163    -80 each
 *   civilians killed > 0                      164    -30 each
 *   civilians rescued, and the mission was won 165   +30 each
 *   aliens killed, won                        166    +20 each
 *   live aliens recovered > 0, won            167    +30 each (Tact_TallyItemRecovery's out-value)
 *   won and nMissionType == AlienBase         169    +500 "Base Destroyed"
 *   lost and nMissionType == XcomBase         170    -700 "Base Lost"
 *   lost, not a base defence, not aborted     168    -400 "Craft Lost"
 *   won, alloys / 8 > 0                       171    alloys / 8
 *   won, elerium > 0                          172    elerium
 *   won, navigation > 0                       173    15 each
 *   won, power source > 0                     174    20 each
 *   won, reproduction > 0                     175    2 each
 *   won, food > 0                             176    1 each
 *   won, examination > 0                      198    the raw count, but 10 each in the score
 *   won, entertainment > 0                    177    2 each
 *   won, surgery > 0                          178    2 each
 *
 * The examination line is the one place where the value column and the score term differ: the
 * column shows the room count (0x36218) and the score adds 10 per room (0x36230). Only one of the
 * three base/craft lines can appear -- won gives 169 for an alien base and nothing otherwise, lost
 * gives 170 for a base defence and otherwise 168 unless the mission was aborted.
 *
 * ## Committing the loot to the working site
 *
 * Each loot line also writes its count into aSmapSites[nSmapSiteCount].auItemQty[obj] and adds it
 * to that object's ObjectRec.uRecovCount (apObjects[obj] @ Map+0x108 + 4*obj, field +0xCC), and
 * sets the local "loot recorded" flag. After the lines, if any loot was recorded -- from these
 * lines or from Tact_TallyCraftRecovery -- the site record is committed (0x364A0..0x36510):
 *
 *   bHasLoot (SmapSite + 0x2E4) = 1
 *   bFlag1   (SmapSite + 0x001) = apBases[apCraft[Map_nCraftIndex]->bHomeBase]->bUnk4
 *   bFlag0   (SmapSite + 0x000) = 8
 *   nSmapSiteCount += 1
 *
 * Both panels' nStyle then take the line count (0x36514, 0x36524), which is what makes the debrief
 * draw exactly the rows that were emitted.
 *
 * ## The Mars restart tail
 *
 * 0x162028 (the replacement-spawn gate) is set to 1 unconditionally (0x36584). Then, when the
 * mission was the Mars/special type with the alternate spawn, it restarts in place as an alien
 * base: Tact_Shutdown(0), Map_nMissionType = 4, Map_bMissionMode = 2, Map_nTerrainVariant = 0,
 * Map_nMapLayers = 0, Map_bUnk15115 = 0, Map_bFrameScratch = 1, Tact_InitMission(0)
 * (0x36594..0x365F8).
 */

/* The debrief's TEXTS records. Every label address in the ARM is aTextRecs + 205*n. */
#define PUFO_DEBRIEF_TEXT_VICTORY        160
#define PUFO_DEBRIEF_TEXT_DEFEAT         161
#define PUFO_DEBRIEF_TEXT_OPERATIVES     162
#define PUFO_DEBRIEF_TEXT_TANKS          163
#define PUFO_DEBRIEF_TEXT_CIV_KILLED     164
#define PUFO_DEBRIEF_TEXT_CIV_RESCUED    165
#define PUFO_DEBRIEF_TEXT_ALIENS_KILLED  166
#define PUFO_DEBRIEF_TEXT_ALIENS_LIVE    167
#define PUFO_DEBRIEF_TEXT_CRAFT_LOST     168
#define PUFO_DEBRIEF_TEXT_BASE_DESTROYED 169
#define PUFO_DEBRIEF_TEXT_BASE_LOST      170
#define PUFO_DEBRIEF_TEXT_ALLOYS         171
#define PUFO_DEBRIEF_TEXT_ELERIUM        172
#define PUFO_DEBRIEF_TEXT_NAVIGATION     173
#define PUFO_DEBRIEF_TEXT_POWER_SOURCE   174
#define PUFO_DEBRIEF_TEXT_REPRODUCTION   175
#define PUFO_DEBRIEF_TEXT_FOOD           176
#define PUFO_DEBRIEF_TEXT_ENTERTAINMENT  177
#define PUFO_DEBRIEF_TEXT_SURGERY        178
#define PUFO_DEBRIEF_TEXT_EXAMINATION    198

#define PUFO_DEBRIEF_UI_SCREEN       13   /* Map_nUiScreen for the debrief screen (0x351EC) */
#define PUFO_BANNER_FADE_TICKS       10   /* dword_113A1C @ Map+0x64 (0x351FC) */
#define PUFO_SOLDIER_FLAG_LOST       0x20 /* Soldier.bFlags bit 5, the dead/lost mark (0x3535C) */
#define PUFO_FACILITY_END_FLAG_SET   1    /* abFacilityGrid[0x618] @ 0x159D80, read signed */
#define PUFO_DEBRIEF_SITE_FLAG0      8    /* SmapSite.bFlag0 as the debrief commits the site */

/* The object each loot line credits, as its apObjects index. */
#define PUFO_LOOT_OBJ_ALLOYS        106
#define PUFO_LOOT_OBJ_ELERIUM       105
#define PUFO_LOOT_OBJ_NAVIGATION    109
#define PUFO_LOOT_OBJ_POWER_SOURCE  107
#define PUFO_LOOT_OBJ_REPRODUCTION  111
#define PUFO_LOOT_OBJ_FOOD          110
#define PUFO_LOOT_OBJ_EXAMINATION   143
#define PUFO_LOOT_OBJ_ENTERTAINMENT 142
#define PUFO_LOOT_OBJ_SURGERY       108

typedef struct TactBattleEndHooks {
    /* Tact_Shutdown @ 0x3d568 needs the app layer's teardown env and Tact_InitMission @ 0x3c550
     * needs the whole mission-init env, so the teardown trio arrives as hooks the way
     * Input_OnLeft's shutdown hooks do. Each hook call site below cites the original address. */
    void (*pfnShutdown)(u8 bKeepSome, void *pUser);
    void (*pfnEndScreenInit)(u8 bGoodEnd, void *pUser);
    void (*pfnInitMission)(i32 nMode, void *pUser);
    /* Tact_ScanVictoryTiles @ 0x33264 probes a 2x2 block of the cell grid at the mission spawn
     * tile; the port injects the probe. */
    bool (*pfnCellHasNoItems)(i32 nX, i32 nY, void *pUser);
    void *pUser;
} TactBattleEndHooks;

typedef struct TactBattleEndEnv {
    /* the tallies and the cleanup, in the order the original runs them */
    const TactTileTallyEnv  *pTiles;   /* 0x35240 */
    const TactUnitTallyEnv  *pUnits;   /* 0x35284 */
    const TactItemTallyEnv  *pItems;   /* 0x352BC */
    const TactCraftTallyEnv *pCraft;   /* 0x352E4 */
    TactBattleFlagsEnv       flags;    /* 0x35380 */
    CameraBoundsCtx          camera;   /* 0x3519C */

    /* the head gates */
    u8        *pbBattleEnded;     /* Map_bBattleEnded @ Map+0x5C */
    u8        *pbTickHold;        /* Map_bTickHold @ Map+0x5A */
    u8        *pbAltOutcomePath;  /* Map_bAltOutcomePath @ Map+0x1A21 */
    u8        *pbMissionVictory;  /* Map_bMissionVictory @ Map+0x150D4 */
    i32       *pnMissionOutcome;  /* Map_nMissionOutcome @ Map+0x1A1C, written and never read */
    u8        *pnMissionType;     /* Map_nMissionType @ Map+0x1511A */
    u8        *pbSpawnAlt;        /* Map_bSpawnAlt @ Map+0x15148 */
    const i8  *pnFacilityEndFlag; /* abFacilityGrid[0x618] @ 0x159D80, read as a signed byte */
    const u8  *pnSpawnTileX;      /* Map_nSpawnTileX @ Map+0x1514C */
    const u8  *pnSpawnTileY;      /* Map_nSpawnTileY @ Map+0x15150 */

    /* the enemy scan */
    Soldier   **apEnemyUnits;     /* apEnemyUnits @ Map+0x151CC */
    const u8  *pnEnemyCount;      /* nEnemyUnitCount @ Map+0x15124 */
    const i32 *pnEnemyReserve;    /* Map_nEnemyReserveCount @ Map+0x15144 */
    const u8  *pnXcomCount;       /* nXcomUnitCount @ Map+0x1511F */

    /* the debrief screen and its two columns */
    u16       *puUiScreen;        /* Map_nUiScreen @ Map+0x3A */
    i32       *pnBannerFadeTicks; /* dword_113A1C @ Map+0x64 */
    u8        *pbCtxBusy;         /* pTactCtx272 @ 0x157180 -> bBusy @ +0xF8 */
    u8        *pbEnemyTurnActive; /* Map_bEnemyTurnActive @ Map+0x5B (was unk_113A13) */
    UiPanel   *pPanelLabel;       /* pUiPanel0 @ 0x1568E8, the label column */
    UiPanel   *pPanelValue;       /* pUiPanel1 @ 0x1568E4, the value column */
    i32       *pnDebriefLine;     /* Map_nDebriefLine @ Map+0x150D0 */
    char      *pszCaption;        /* Map_szMissionCaption @ Map+0x15094 */
    char      *pszScore;          /* Map_szMissionScore @ Map+0x150B2 */
    const char *pTextRecs;        /* aTextRecs @ 0xB38D0, PUFO_TEXT_REC_STRIDE apart */

    /* the loot commit */
    BaseRec   **apBases;          /* apBases @ Map+0x1174 */
    i32       *pnSiteCount;       /* nSmapSiteCount @ 0x186410, the slot the commit fills. The
                                   * craft tally env carries the same global read-only; the debrief
                                   * is what advances it, so it needs its own writable handle. */

    /* the tail */
    u8        *pbSpawnOnceFlag;   /* 0x162028, the replacement-spawn gate */
    i32       *pnFundPending;     /* g_nFundPending @ 0x1865BC */
    u8        *pbMissionMode;     /* Map_bMissionMode @ Map+0x1511D */
    u8        *pnTerrainVariant;  /* Map_nTerrainVariant @ Map+0x15111 */
    u8        *pnMapLayers;       /* Map_nMapLayers @ Map+0x15114 */
    u8        *pbUnk15115;        /* Map_bUnk15115 @ Map+0x15115 */
    u8        *pbFrameScratch;    /* Map_bFrameScratch @ Map+0x254BC */

    TactBattleEndHooks hooks;
} TactBattleEndEnv;

/* Tact_CheckBattleEnd @ 0x34fbc. */
void Tact_CheckBattleEnd(const TactBattleEndEnv *pEnv, u8 bAbort);

#endif /* PUFO_TACTEND_H */
