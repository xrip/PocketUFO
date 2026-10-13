/* tests/test_tactinit.c - Tact_InitMission @ 0x3c550.
 *
 * Tact_InitMission is a composition root: it owns no algorithm of its own, it decides
 * what state each already-ported callee sees and in what order. So the fixture supplies
 * the whole world (the Map-block scalars it reaches through pointers, the 14400-slot cell
 * array, 160 object records, the deploy tables, the hooks) and the assertions are about
 * the state the function itself writes.
 *
 * What is asserted, and why these things:
 *   - the prologue flag block, field by field, because it is the part of the function
 *     nothing else reproduces and every value is a constant;
 *   - the selected-weapon record and the three object-pointer copies, because their
 *     indices into apObjects are the only place those globals are set;
 *   - the five panels' line counts, style, font, positions and alignment, because the
 *     layout block is 250 instructions of pure constants;
 *   - the deploy-point sweep, cell by cell, because the record indexing
 *     (4*n + 104*(nMapLayers-1)) and the 3600*layer + 60*y + x cell formula are the
 *     easy thing to get wrong there;
 *   - the bFlags29 bit-4 sweep, because its rule changes with the mission type and the
 *     alien-base exception is three conditions on the layer, the row and the column;
 *   - the showship reveal against the shipped showship.dat, because the header words are
 *     count-then-length and the four-layer walk is what makes it a reveal rather than a
 *     single-layer stamp;
 *   - the briefing block's text assembly and the two hand-written lines, because the
 *     order (wrap first, then append, then nLineCount += 2) is not obvious from the
 *     pseudocode.
 *
 * Game_RandU16 is the CRT rand(), so srand() pins a run. The two random mission
 * parameters are asserted as ranges, not values.
 */
#include "asset/pufo_asset.h"
#include "game/pufo_mapctx.h"
#include "game/pufo_ship.h"
#include "game/pufo_tactinit.h"
#include "game/pufo_template.h"
#include "game/pufo_tactui.h"
#include "game/pufo_timer.h"
#include "game/pufo_unit.h"
#include "pufo_common.h"
#include "ui/pufo_panel.h"
#include "util/pufo_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CELL_SLOTS   14400
#define OBJECT_COUNT 160
#define SOLDIER_CAP  64
#define ENEMY_CAP    128
#define ITEM_CAP     64

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
        printf("  ok   %s (%d)\n", pszWhat, nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, nGot, nWant);
        g_nFailures += 1;
    }
}

/* ------------------------------------------------------------------ the world */

static GameMap       s_Map;
static TactInitEnv   s_Env;
static TactState     s_State;
static RmpData       s_Rmp;
static MapCell      *s_apCells[CELL_SLOTS];
static PufoFont      s_Font;
static LosTables     s_Loft;

static ObjectRecFull s_aObjects[OBJECT_COUNT];
static ObjectRec    *s_apObjects[OBJECT_COUNT];
static TactItem     *s_apTactItems[ITEM_CAP];
static i32           s_nTactItems;
static i32           s_nFlobjsWide;

static Soldier      *s_apSoldiers[SOLDIER_CAP];
static i32           s_nSoldierCount;
static i32           s_nNextSoldierId;
static Soldier      *s_apEnemyUnits[ENEMY_CAP];
static u8            s_nEnemyCount;
static Soldier      *s_apHidden[SOLDIER_CAP];
static i32           s_nReserve;
static char          s_aszUnitName[23][40];

static Ship          s_aCraft[2];
static Ship         *s_apCraft[2];
static u16           s_nObjectCount;
static u8            s_nCraftCount;
static i32           s_aCargoSplit[OBJECT_COUNT];
static i32           s_aCargoWork[OBJECT_COUNT * PUFO_CARGO_WORK_PER_OBJ];
static u8            s_nCraftIndex;
static u8            s_bMissionMode;
static i8            s_nBaseIndex;
static i8            s_nSelectedBase;

static VisRay        s_aRays[PUFO_VIS_RAY_COUNT];
static u8            s_aSteps[PUFO_VIS_TABLE_BYTES];
static u8            s_aLightmap[PUFO_TACTINIT_LIGHTMAP_LEN];
static void         *s_pShowShip;
static PadImage     *s_pBriefPad;
static MapCell      *s_pDeployCell;
static TactWeaponSel s_WeaponSel;
static u8            s_aSentinel[0x18];


/* The Map-block scalars the context struct does not model. */
static u8   s_bTactFlag, s_bUnkMap01, s_bPlayerInput, s_bFire0, s_bFire2;
static u8   s_bTickHold, s_bBattleEnded, s_bSlot8_2, s_bSlot8_3, s_bSweepArmed;
static u8   s_bAltPath, s_bUnk1511E, s_bSpawnAlt, s_bDrawIso, s_bLight, s_bUnk254D4;
static u8   s_nCraftX, s_nCraftY;
static u8   s_nTerrainVariant, s_nTier, s_bUnk15115;
static i32  s_nTierSource, s_nShotX, s_nShotY, s_nSpawnX, s_nSpawnY;
static u16  s_uUiScreen;
static i32  s_nUiSlot1, s_nUiSlot9, s_nUiSlot10, s_nUiSlot11;
static i32  s_nUnk254B4, s_nUnk254B8, s_nGamma;
static GameClock s_Clock;
static ObjectRec *s_pObj156A88, *s_pObj156910, *s_pObj156A58;

static u8 s_aTileTypes[36];
static u8 s_aDeployOffsets[PUFO_DEPLOY_RECORDS_GROUP * 3 * PUFO_DEPLOY_RECORD_BYTES];
static u8 s_aDeployCounts[3] = { 14, 12, 26 };
static u8 s_aHwpsOffsets[12 * 4];
static i32 s_aScatterMid[64], s_aScatterLast[64], s_nScatterCount;
static i32 s_nDeployedHwps;

/* The briefing text blob: a stand-in for the loaded texts.dat records. */
static u8 s_aTextBlob[0x39000];

static InvHitState  s_Hit;
static InvDropCtx   s_Drop;
static InvDropHooks s_DropHooks;
static TactUiState  s_Ui;

/* ------------------------------------------------------------------ hook records */

static i32 s_nPanelCtor[PUFO_TACTINIT_PANELS];
static i32 s_nPanelCtorCalls;
static i32 s_nStyleCalls;
static i32 s_nWrapCalls;
static u8  s_nWrapStart[4];
static i32 s_nOpenInvCalls;
static i32 s_nLoadTactSpritesCalls;
static i32 s_nSoundInitCalls, s_nMusicCalls, s_nMusicVolCalls, s_nSfxVolCalls;
static i32 s_nMusicTrack;
static char s_szSoundPrefix[32];
static i32 s_nTimerSets;
static u32 s_nLastTimerId;
static u32 s_nLastTimerMs;

static bool Hook_PanelCtor(UiPanel *pPanel, i32 nLineCount, void *pUser)
{
    (void)pUser;
    if (s_nPanelCtorCalls < PUFO_TACTINIT_PANELS) {
        s_nPanelCtor[s_nPanelCtorCalls] = nLineCount;
    }
    s_nPanelCtorCalls += 1;
    return UiPanel_ctor(pPanel, nLineCount);
}

static void Hook_PanelStyle(UiPanel *pPanel, u8 b6, u8 b7, u8 b8, u8 b9, u8 b10, u8 b11,
                            void *pUser)
{
    (void)pUser;
    s_nStyleCalls += 1;
    UiPanel_SetBytes6to11(pPanel, b6, b7, b8, b9, b10, b11);
}

static i32 Hook_PanelWrap(UiPanel *pPanel, u8 nMaxW, u8 nLineStart, const char *pszText,
                          void *pUser)
{
    (void)pUser;
    if (s_nWrapCalls < 4) {
        s_nWrapStart[s_nWrapCalls] = nLineStart;
    }
    s_nWrapCalls += 1;
    return UiPanel_WrapText(pPanel, nMaxW, nLineStart, pszText);
}

static bool Hook_LoadUiPads(void *pUser) { (void)pUser; return true; }
static void Hook_InitUiControls(void *pUser) { (void)pUser; }
static void Hook_LoadTactSprites(void *pUser) { (void)pUser; s_nLoadTactSpritesCalls += 1; }
static bool Hook_SoundInit(const char *pszPrefix, void *pUser)
{
    (void)pUser;
    s_nSoundInitCalls += 1;
    pufo_strcpy(s_szSoundPrefix, pszPrefix);
    return true;
}
static void Hook_Music(i32 nTrack, void *pUser) { (void)pUser; s_nMusicCalls += 1; s_nMusicTrack = nTrack; }
static void Hook_MusicVol(void *pUser) { (void)pUser; s_nMusicVolCalls += 1; }
static void Hook_SfxVol(void *pUser) { (void)pUser; s_nSfxVolCalls += 1; }
static bool Hook_OpenInventory(void *pUser) { (void)pUser; s_nOpenInvCalls += 1; return true; }

static void Hook_KillUnit(Soldier *pUnit, void *pUser) { (void)pUnit; (void)pUser; }
static u8 Hook_Weight(TactItem *pItem, void *pUser) { (void)pItem; (void)pUser; return 0; }
static u8 Hook_TwoHanded(TactItem *pItem, void *pUser) { (void)pItem; (void)pUser; return 0; }
static u8 Hook_AmmoCap(TactItem *pItem, void *pUser) { (void)pItem; (void)pUser; return 255; }
static u8 Hook_AmmoWeight(i32 nIndex, void *pUser) { (void)nIndex; (void)pUser; return 0; }

static const InvStatHooks s_InvHooks = {
    Hook_Weight, Hook_TwoHanded, Hook_AmmoCap, Hook_AmmoWeight, NULL
};

static void Hook_TimerSet(HWND hWnd, u32 nId, u32 nMs, void *pUser)
{
    (void)hWnd; (void)pUser;
    s_nTimerSets += 1;
    s_nLastTimerId = nId;
    s_nLastTimerMs = nMs;
}
static void Hook_TimerKill(HWND hWnd, u32 nId, void *pUser) { (void)hWnd; (void)nId; (void)pUser; }

static void Hook_KillTimers(void *pUser) { (void)pUser; }
static void Hook_StartTimers(void *pUser) { (void)pUser; }

/* ------------------------------------------------------------------ fixture */

static MapCell *Cell(i32 nX, i32 nY, i32 nLayer);

static MapCell *TestLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    return Cell(nX, nY, nLayer);
}

static MapCell *Cell(i32 nX, i32 nY, i32 nLayer)
{
    if (nLayer < 0 || nLayer >= PUFO_MAP_LAYERS) return NULL;
    if (nX < 0 || nX >= PUFO_MAP_CELL_DIM || nY < 0 || nY >= PUFO_MAP_CELL_DIM) return NULL;
    return s_apCells[PUFO_TERRAIN_CELLS_PER_LAYER * nLayer + PUFO_MAP_CELL_DIM * nY + nX];
}

static void Fixture_Reset(void)
{
    memset(&s_Map, 0, sizeof(s_Map));
    /* Mission type 0 makes the terrain builder index aPlane[-1]; that is the original
     * defect already recorded in bugs.tsv, so the fixture uses a valid type. */
    s_Map.nMissionType = 1;
    memset(&s_Env, 0, sizeof(s_Env));
    memset(&s_State, 0, sizeof(s_State));
    memset(s_apCells, 0, sizeof(s_apCells));
    memset(s_aObjects, 0, sizeof(s_aObjects));
    memset(s_apTactItems, 0, sizeof(s_apTactItems));
    memset(s_apSoldiers, 0, sizeof(s_apSoldiers));
    memset(s_apEnemyUnits, 0, sizeof(s_apEnemyUnits));
    memset(s_apHidden, 0, sizeof(s_apHidden));
    memset(s_aszUnitName, 0, sizeof(s_aszUnitName));
    memset(s_aTileTypes, 0, sizeof(s_aTileTypes));
    memset(s_aDeployOffsets, 0, sizeof(s_aDeployOffsets));
    memset(s_aHwpsOffsets, 0, sizeof(s_aHwpsOffsets));
    memset(s_aScatterMid, 0, sizeof(s_aScatterMid));
    memset(s_aScatterLast, 0, sizeof(s_aScatterLast));
    memset(s_aSentinel, 0, sizeof(s_aSentinel));
    memset(s_aTextBlob, 0, sizeof(s_aTextBlob));
    memset(s_nPanelCtor, 0, sizeof(s_nPanelCtor));
    memset(s_nWrapStart, 0, sizeof(s_nWrapStart));
    memset(s_szSoundPrefix, 0, sizeof(s_szSoundPrefix));
    s_nPanelCtorCalls = 0;
    s_nStyleCalls = 0;
    s_nWrapCalls = 0;
    s_nOpenInvCalls = 0;
    s_nLoadTactSpritesCalls = 0;
    s_nSoundInitCalls = 0;
    s_nMusicCalls = 0;
    s_nMusicVolCalls = 0;
    s_nSfxVolCalls = 0;
    s_nMusicTrack = -1;
    s_nTimerSets = 0;
    s_nLastTimerId = 0;
    s_nLastTimerMs = 0;
    s_nSoldierCount = 0;
    s_nNextSoldierId = 1;
    s_nEnemyCount = 0;
    s_nReserve = 0;
    s_nTactItems = 0;
    s_nFlobjsWide = 0;
    s_nScatterCount = 0;
    s_nDeployedHwps = 0;
    memset(s_aCraft, 0, sizeof(s_aCraft));
    s_apCraft[0] = &s_aCraft[0];
    s_apCraft[1] = &s_aCraft[1];
    memset(s_aCargoSplit, 0, sizeof(s_aCargoSplit));
    memset(s_aCargoWork, 0, sizeof(s_aCargoWork));
    s_nCraftCount = 1;
    s_nCraftIndex = 0;
    s_bMissionMode = 0;
    s_nBaseIndex = 0;
    s_nSelectedBase = 0;
    s_pShowShip = NULL;
    s_pBriefPad = NULL;
    s_pDeployCell = NULL;
    memset(&s_Loft, 0, sizeof(s_Loft));
    PufoFont_Init(&s_Font);

    for (i32 i = 0; i < OBJECT_COUNT; ++i) {
        s_apObjects[i] = &s_aObjects[i];
        s_aObjects[i].anAmmoTypeId[0] = 0;
        s_aObjects[i].bWeight = 0;
        s_aObjects[i].uRecovScore = 0;   /* no hardware soldiers from the craft stores */
    }
    s_aObjects[0].bAmmoCap = 100;

    for (i32 i = 9; i < 23; ++i) {
        pufo_strcpy(s_aszUnitName[i], "ALIEN");
    }

    s_Drop.pHooks = &s_DropHooks;
    s_Drop.pHit = &s_Hit;
    s_Drop.pUi = &s_Ui;

    /* --- Map-block scalars --- */
    s_bTactFlag = 0; s_bUnkMap01 = 0; s_bPlayerInput = 0; s_bFire0 = 0; s_bFire2 = 0;
    s_bTickHold = 0; s_bBattleEnded = 0; s_bSlot8_2 = 0; s_bSlot8_3 = 0; s_bSweepArmed = 0;
    s_bAltPath = 1; s_bUnk1511E = 0; s_bSpawnAlt = 0; s_bDrawIso = 1; s_bLight = 0;
    s_bUnk254D4 = 1;
    s_nCraftX = 3; s_nCraftY = 3;
    s_nTerrainVariant = 1; s_nTier = 0; s_nTierSource = 0; s_bUnk15115 = 20;
    s_nShotX = 0; s_nShotY = 0; s_nSpawnX = 0; s_nSpawnY = 0;
    s_uUiScreen = 7;
    s_nUiSlot1 = 0x99; s_nUiSlot9 = 0x99; s_nUiSlot10 = 0x99; s_nUiSlot11 = 0x99;
    s_nUnk254B4 = 0x99; s_nUnk254B8 = 0x99; s_nGamma = 3;
    s_Clock.nSpeed = 1;
    s_Clock.bPaused = false;
    s_pObj156A88 = NULL; s_pObj156910 = NULL; s_pObj156A58 = NULL;
    memset(&s_WeaponSel, 0, sizeof(s_WeaponSel));

    /* --- the mission tables --- */
    s_aDeployCounts[0] = 14; s_aDeployCounts[1] = 12; s_aDeployCounts[2] = 26;
    for (i32 i = 0; i < 36; ++i) s_aTileTypes[i] = 0x14;   /* suppress the stamping passes */

    /* --- wire the env --- */
    s_Env.pMap             = &s_Map;
    s_Env.apCells          = s_apCells;
    s_Env.pTactState       = &s_State;
    s_Env.pRmp             = &s_Rmp;
    s_Env.pLoft            = &s_Loft;
    s_Env.pFont            = &s_Font;
    s_Env.apObjects        = s_apObjects;
    s_Env.apSoldiers       = s_apSoldiers;
    s_Env.pnSoldierCount   = &s_nSoldierCount;
    s_Env.nSoldierCapacity = SOLDIER_CAP;
    s_Env.apTactItems      = s_apTactItems;
    s_Env.pnTactItemCount  = &s_nTactItems;
    s_Env.pnFlobjsWide     = &s_nFlobjsWide;
    s_Env.nObjectCount     = OBJECT_COUNT;
    s_Env.ppShowShip       = &s_pShowShip;
    s_Env.pLightmap        = s_aLightmap;
    s_Env.ppBriefPad       = &s_pBriefPad;
    s_Env.ppDeployCell     = &s_pDeployCell;
    s_Env.pWeaponSel       = &s_WeaponSel;
    s_Env.pUnk138EA0       = s_aSentinel;
    s_Env.ppObj156A88      = &s_pObj156A88;
    s_Env.ppObj156910      = &s_pObj156910;
    s_Env.ppObj156A58      = &s_pObj156A58;
    s_Env.apVisRays        = s_aRays;
    s_Env.paVisSteps       = s_aSteps;
    s_Env.pClock           = &s_Clock;
    s_Env.pDeployOffsets   = s_aDeployOffsets;
    s_Env.pDeployPointCounts = s_aDeployCounts;
    s_Env.pTileTypes       = s_aTileTypes;
    s_Env.pTextRecs        = s_aTextBlob;
    s_Env.nTextRecCount    = sizeof s_aTextBlob;
    s_Env.pszBriefLine     = "BRIEF-LINE";

    s_Env.pbTactFlag    = &s_bTactFlag;
    s_Env.pbUnkMap01    = &s_bUnkMap01;
    s_Env.pbPlayerInput = &s_bPlayerInput;
    s_Env.puUiScreen    = &s_uUiScreen;
    s_Env.pbFire0       = &s_bFire0;
    s_Env.pbFire2       = &s_bFire2;
    s_Env.pbGameTickHold = &s_bTickHold;
    s_Env.pbBattleEnded = &s_bBattleEnded;
    s_Env.pnUiSlot1     = &s_nUiSlot1;
    s_Env.pbSweepArmed  = &s_bSweepArmed;
    s_Env.pbSlot8Byte2  = &s_bSlot8_2;
    s_Env.pbSlot8Byte3  = &s_bSlot8_3;
    s_Env.pnUiSlot9     = &s_nUiSlot9;
    s_Env.pnUiSlot10    = &s_nUiSlot10;
    s_Env.pnUiSlot11    = &s_nUiSlot11;
    s_Env.pbAltOutcomePath = &s_bAltPath;
    s_Env.pnGamma       = &s_nGamma;
    s_Env.pbUnkMap1511E = &s_bUnk1511E;
    s_Env.pnCraftMapX   = &s_nCraftX;
    s_Env.pnCraftMapY   = &s_nCraftY;
    s_Env.pnTerrainVariant = &s_nTerrainVariant;
    s_Env.pnTier           = &s_nTier;
    s_Env.pnTierSource     = &s_nTierSource;
    s_Env.pbUnk15115       = &s_bUnk15115;
    s_Env.pnShotX          = &s_nShotX;
    s_Env.pnShotY          = &s_nShotY;
    s_Env.pnSpawnTileX     = &s_nSpawnX;
    s_Env.pnSpawnTileY     = &s_nSpawnY;
    s_Env.pbSpawnAlt    = &s_bSpawnAlt;
    s_Env.pbDrawIsoLayer = &s_bDrawIso;
    s_Env.pbLightEnabled = &s_bLight;
    s_Env.pnUnkMap254B4 = &s_nUnk254B4;
    s_Env.pnUnkMap254B8 = &s_nUnk254B8;
    s_Env.pbUnkMap254D4 = &s_bUnk254D4;

    for (i32 i = 0; i < PUFO_TACTINIT_PANELS; ++i) s_Env.apPanel[i] = NULL;

    /* --- callee contexts: the world-dependent half --- */
    s_Env.teamSelect.apSoldiers    = s_apSoldiers;
    s_Env.teamSelect.nSoldierCount = 0;
    s_Env.teamSelect.apCraft       = s_apCraft;
    s_Env.teamSelect.bMissionMode  = 0;
    s_Env.teamSelect.nCraftIndex   = 0;
    s_Env.teamSelect.nSelectedBase = 0;
    s_Env.teamSelect.pInvHooks     = &s_InvHooks;

    s_Env.spawnAliens.create.apSoldiers      = s_apSoldiers;
    s_Env.spawnAliens.create.pnSoldierCount  = &s_nSoldierCount;
    s_Env.spawnAliens.create.pnNextSoldierId = &s_nNextSoldierId;
    s_Env.spawnAliens.apEnemyUnits           = s_apEnemyUnits;
    s_Env.spawnAliens.pnEnemyUnitCount       = &s_nEnemyCount;
    s_Env.spawnAliens.apHiddenSoldiers       = s_apHidden;
    s_Env.spawnAliens.pnEnemyReserveCount    = &s_nReserve;
    s_Env.spawnAliens.apTactItems            = s_apTactItems;
    s_Env.spawnAliens.pnTactItemCount        = &s_nTactItems;
    s_Env.spawnAliens.apObjects              = s_apObjects;
    s_Env.spawnAliens.pFont                  = &s_Font;
    s_Env.spawnAliens.pRmp                   = &s_Rmp;
    s_Env.spawnAliens.pfnLookupCell          = TestLookup;
    s_Env.spawnAliens.pInvHooks              = &s_InvHooks;
    s_Env.spawnAliens.pfnKillUnit            = Hook_KillUnit;
    s_Env.spawnAliens.panUnitName            = &s_aszUnitName[0][0];
    s_Env.spawnAliens.nMissionMode           = 0;
    s_Env.spawnAliens.nTerrainVariant        = 1;
    s_Env.spawnAliens.nAlienTier             = 0;
    s_Env.spawnAliens.nAlienPoolScale        = 20;
    s_Env.spawnAliens.nAlienSetIndex         = 0;
    s_Env.spawnAliens.nFourthRaceCount       = 0;
    for (i32 i = 0; i < 4; ++i) s_Env.spawnAliens.abSpriteSet[i] = PUFO_SPRITE_SET_NONE;

    s_Env.cargo.apObjects       = s_apObjects;
    s_nObjectCount = OBJECT_COUNT;
    s_Env.cargo.pnObjectCount   = &s_nObjectCount;
    s_Env.cargo.apCraft         = s_apCraft;
    s_Env.cargo.pnCraftCount    = &s_nCraftCount;
    s_Env.cargo.apTactItems     = s_apTactItems;
    s_Env.cargo.pnTactItemCount = &s_nTactItems;
    s_Env.cargo.panCargoSplit   = s_aCargoSplit;
    s_Env.cargo.panCargoWork    = s_aCargoWork;
    s_Env.cargo.pfnLookupCell   = TestLookup;
    s_Env.cargo.nCraftIndex     = 0;
    s_Env.cargo.nBaseIndex      = 0;

    s_Env.equip.pHit  = &s_Hit;
    s_Env.equip.pDrop = &s_Drop;

    s_Env.deploy.create.apSoldiers      = s_apSoldiers;
    s_Env.deploy.create.pnSoldierCount  = &s_nSoldierCount;
    s_Env.deploy.create.pnNextSoldierId = &s_nNextSoldierId;
    s_Env.deploy.pnDeployedHwps         = &s_nDeployedHwps;
    s_Env.deploy.pHwpsOffsets           = s_aHwpsOffsets;
    s_Env.deploy.panScatterMid          = s_aScatterMid;
    s_Env.deploy.panScatterLast         = s_aScatterLast;
    s_Env.deploy.pnScatterCount         = &s_nScatterCount;
    s_Env.deploy.pTileTypes             = s_aTileTypes;

    s_Env.select.panRankLow   = "RANK";
    s_Env.select.panRankOther = "RANK";

    s_Env.hooks.pfnPanelCtor         = Hook_PanelCtor;
    s_Env.hooks.pfnPanelStyle6to11   = Hook_PanelStyle;
    s_Env.hooks.pfnPanelWrapText     = Hook_PanelWrap;
    s_Env.hooks.pfnLoadUiPads        = Hook_LoadUiPads;
    s_Env.hooks.pfnInitUiControls    = Hook_InitUiControls;
    s_Env.hooks.pfnLoadTactSprites   = Hook_LoadTactSprites;
    s_Env.hooks.pfnSoundInit         = Hook_SoundInit;
    s_Env.hooks.pfnSoundPlayMusic    = Hook_Music;
    s_Env.hooks.pfnSoundApplyMusicVolume = Hook_MusicVol;
    s_Env.hooks.pfnSoundApplySfxVolume   = Hook_SfxVol;
    s_Env.hooks.pfnOpenInventory     = Hook_OpenInventory;

    const TimerBackend backend = { Hook_TimerSet, Hook_TimerKill, NULL };
    Timer_SetBackend(&backend);
    const GameTimers timers = { Hook_KillTimers, Hook_StartTimers, NULL };
    GameSpeed_SetTimers(&timers);
}

static void RunMission(i32 nMode)
{
    if (!Tact_InitMission(&s_Env, nMode)) {
        printf("  FAIL Tact_InitMission returned false\n");
        g_nFailures += 1;
    }
}

/* ------------------------------------------------------------------ tests */

static void TestPrologueFlags(void)
{
    printf("prologue flag block\n");
    Fixture_Reset();
    /* The terrain pass re-rolls the craft tile through Game_RandU16, so the run is
     * pinned here to keep the derived expectations reproducible. */
    srand(1);
    RunMission(0);

    Check(s_nUnk254B4 >= 10 && s_nUnk254B4 <= 41, "unk_138E6C is rand(0x28) + 10");
    Check(s_nUnk254B8 >= 0 && s_nUnk254B8 <= 19, "unk_138E70 is rand(0x14)");
    CheckEqI32(s_bAltPath, 0, "unk_1153D9 cleared");
    CheckEqI32(s_bBattleEnded, 0, "bBattleEnded cleared");
    CheckEqI32(s_nUiSlot1, 0, "apUiSlotsA[1] cleared");
    CheckEqI32(s_bTickHold, 0, "bGameTickHold cleared");
    CheckEqI32(s_bFire0, 0, "bFire0 cleared");
    CheckEqI32(s_bFire2, 0, "bFire2 cleared");
    CheckEqI32(s_Map.bFireStatus1, 0, "bFireStatus1 cleared");
    CheckEqI32(s_Map.bActionLatch, 0, "bActionLatch cleared");
    CheckEqI32(s_bPlayerInput, 1, "bPlayerInputEnabled set");
    CheckEqI32(s_bTactFlag, 1, "bTactFlag set");
    CheckEqI32(s_nUiSlot9, 0, "apUiSlotsA[9] cleared");
    CheckEqI32(s_bSlot8_2, 0, "BYTE2(apUiSlotsA[8]) cleared");
    CheckEqI32(s_bSlot8_3, 0, "BYTE3(apUiSlotsA[8]) cleared");
    CheckEqI32(s_nUiSlot10, 10, "apUiSlotsA[10] set to 10");
    CheckEqI32(s_Map.geom.nDimX, 6, "nMapDimX is 6");
    CheckEqI32(s_Map.geom.nDimY, 6, "nMapDimY is 6");
    CheckEqI32(s_Map.status.nCount, 0, "status stack count cleared");
    CheckEqI32(s_Map.nCivilianCount, 0, "civilian count cleared");
    CheckEqI32(s_uUiScreen, 0, "nUiScreen cleared");
    CheckEqI32(s_bUnk254D4, 0, "unk_138E8C cleared only on the new-mission path");
    CheckEqI32(s_State.nQueueLen, 0, "TactState.nQueueLen cleared");
    CheckEqI32(s_State.bQueueActive, 0, "TactState.bQueueActive cleared");
    CheckEqI32(s_State.gap_70112[1], 0, "TactState+0x111E1 cleared");
    CheckEqI32(s_State.bUnk70114, 0, "TactState.bUnk70114 cleared");
    CheckEqI32(s_State.bPhaseActive, 0, "TactState.bPhaseActive cleared");
    CheckEqI32(s_bSweepArmed, 1, "apUiSlotsA[2] armed by the tail");
    CheckEqI32(s_bDrawIso, 1, "bDrawIsoLayer set by the tail");
    CheckEqI32(s_nOpenInvCalls, 0, "no inventory screen with an empty roster");
    CheckEqI32(s_bUnkMap01, 0, "unk_1139B9 cleared by the no-XCom branch");
}

static void TestWeaponSelAndObjectCopies(void)
{
    printf("selected weapon record and object pointer copies\n");
    Fixture_Reset();
    s_aObjects[1].anAmmoTypeId[0] = 77;
    s_WeaponSel.pObj = NULL;
    RunMission(0);

    Check(s_WeaponSel.pObj == &s_aObjects[1], "unk_10DB90.pObj is apObjects[1]");
    CheckEqI32(s_WeaponSel.bConst10, 10, "unk_10DB96 is the constant 10");
    CheckEqI32(s_WeaponSel.uAmmoTypeId, 77, "unk_10DB94 is apObjects[1]->anAmmoTypeId[0]");
    Check(memcmp(s_aSentinel, &s_apObjects[0], sizeof(void *)) == 0,
          "unk_138EA0 dword 0 is apObjects[0]");
    CheckEqI32(*(i32 *)(s_aSentinel + 0x14), 0x7D00, "unk_138EB4 is 0x7D00");
    Check(s_pObj156A88 == &s_aObjects[PUFO_SHOT_OBJ_VARIANT3],
          "unk_156A88 is apObjects[51]");
    Check(s_pObj156910 == &s_aObjects[PUFO_SHOT_OBJ_VARIANT2],
          "unk_156910 is apObjects[24]");
    Check(s_pObj156A58 == &s_aObjects[141], "unk_156A58 is apObjects[141]");
}

static void TestPanelLayout(void)
{
    printf("the five message panels\n");
    Fixture_Reset();
    RunMission(0);

    CheckEqI32(s_nPanelCtorCalls, PUFO_TACTINIT_PANELS, "five panels constructed");
    CheckEqI32(s_nPanelCtor[0], 14, "panel 0 line count 14");
    CheckEqI32(s_nPanelCtor[1], 14, "panel 1 line count 14");
    CheckEqI32(s_nPanelCtor[2], 14, "panel 2 line count 14");
    CheckEqI32(s_nPanelCtor[3], 14, "panel 3 line count 14");
    CheckEqI32(s_nPanelCtor[4], 28, "briefing panel line count 28");
    CheckEqI32(s_nStyleCalls, PUFO_TACTINIT_PANELS, "each panel gets its colour triple pair");

    const i32 aWantX[5] = { 10, 200, 10, 230, 120 };
    const i32 aWantY[5] = { 30,  30, 55,  55,  30 };
    const i32 aWantAlign[5] = { 0, 0, 0, 2, 1 };
    const u8  aWant7072[5] = { 0, 0, 1, 1, 0 };
    const u8  aWant7074[5] = { 0, 1, 0, 0, 0 };

    bool bOk = true;
    for (i32 i = 0; i < PUFO_TACTINIT_PANELS; ++i) {
        const UiPanel *p = s_Env.apPanel[i];
        if (p == NULL) { bOk = false; continue; }
        if (p->nStyle != 14 || p->nFont != 16) bOk = false;
        if (p->nPosX != aWantX[i] || p->nPosY != aWantY[i]) bOk = false;
        if (p->nUnk7076 != aWantAlign[i]) bOk = false;
        if (p->bFlag7072 != aWant7072[i]) bOk = false;
        if (p->bFlag7073 != 0) bOk = false;
        if (p->bFlag7074 != aWant7074[i]) bOk = false;
        if (p->gap_0[6] != 0xF0 || p->gap_0[9] != 0x14) bOk = false;
    }
    Check(bOk, "positions, style, font, alignment and flags match the ARM");

    for (i32 i = 0; i < PUFO_TACTINIT_PANELS; ++i) {
        if (s_Env.apPanel[i] != NULL) {
            UiPanel_FreeLines(s_Env.apPanel[i]);
            free(s_Env.apPanel[i]);
            s_Env.apPanel[i] = NULL;
        }
    }
}

static void TestDeployCellFlags(void)
{
    printf("deploy-point sweep\n");
    Fixture_Reset();
    s_Map.nMapLayers = 3;
    s_nCraftX = 2;
    s_nCraftY = 1;
    /* Group 2 (nMapLayers 3) starts at record 52. Give three records offsets. */
    s_aDeployCounts[2] = 3;
    s_aDeployOffsets[52 * PUFO_DEPLOY_RECORD_BYTES + 0] = 1;   /* dx */
    s_aDeployOffsets[52 * PUFO_DEPLOY_RECORD_BYTES + 1] = 2;   /* dy */
    s_aDeployOffsets[52 * PUFO_DEPLOY_RECORD_BYTES + 2] = 1;   /* layer */
    s_aDeployOffsets[53 * PUFO_DEPLOY_RECORD_BYTES + 0] = 5;
    s_aDeployOffsets[53 * PUFO_DEPLOY_RECORD_BYTES + 1] = 0;
    s_aDeployOffsets[53 * PUFO_DEPLOY_RECORD_BYTES + 2] = 3;
    s_aDeployOffsets[54 * PUFO_DEPLOY_RECORD_BYTES + 0] = 0;
    s_aDeployOffsets[54 * PUFO_DEPLOY_RECORD_BYTES + 1] = 0;
    s_aDeployOffsets[54 * PUFO_DEPLOY_RECORD_BYTES + 2] = 0;
    s_bUnk1511E = 1;   /* keep the reveal/clear passes from touching the flags */

    RunMission(0);

    /* Map_BuildMissionTerrain re-rolls the craft tile (Map_nCraftMapX/Y @ 0x15139/3A),
     * so the sweep's base is whatever the terrain pass left, not the fixture value. */
    const i32 nBaseX = s_nCraftX;
    const i32 nBaseY = s_nCraftY;

    MapCell *pA = Cell(10 * nBaseX + 1, 10 * nBaseY + 2, 1);
    MapCell *pB = Cell(10 * nBaseX + 5, 10 * nBaseY + 0, 3);
    MapCell *pC = Cell(10 * nBaseX + 0, 10 * nBaseY + 0, 0);
    MapCell *pOther = Cell(7, 7, 2);

    Check(pA != NULL && pB != NULL && pC != NULL, "the three named deploy cells exist");
    Check(pA != NULL && (pA->nFlags40 & PUFO_DEPLOY_CELL_FLAG) == PUFO_DEPLOY_CELL_FLAG,
          "record 0 stamps nFlags40 with 0x78");
    Check(pB != NULL && (pB->nFlags40 & PUFO_DEPLOY_CELL_FLAG) == PUFO_DEPLOY_CELL_FLAG,
          "record 1 stamps its own layer");
    Check(pC != NULL && (pC->nFlags40 & PUFO_DEPLOY_CELL_FLAG) == PUFO_DEPLOY_CELL_FLAG,
          "record 2 stamps layer 0");
    Check(pOther != NULL && (pOther->nFlags40 & PUFO_DEPLOY_CELL_FLAG) == 0,
          "an unnamed cell is left alone");
}

static void TestMissionCellFlags(void)
{
    printf("bFlags29 bit-4 sweep\n");

    /* Mission type 4: set only on layers 0 and 1, last cell row, from 10*(dimY-3) right. */
    Fixture_Reset();
    s_Map.nMapLayers = 2;
    s_Map.nMissionType = PUFO_MISSION_TYPE_ALIEN_BASE;
    s_bUnk1511E = 1;
    RunMission(0);

    const i32 nLastRow = 10 * 6 - 10;      /* 50 */
    const i32 nFirstCol = 10 * (6 - 3);    /* 30 */

    Check(Cell(nFirstCol, nLastRow, 0)->bFlags29 & 0x10,
          "alien base: layer 0, last row, first column is set");
    Check(Cell(59, nLastRow, 1)->bFlags29 & 0x10,
          "alien base: layer 1, last row, last column is set");
    Check((Cell(nFirstCol, nLastRow, 2)->bFlags29 & 0x10) == 0,
          "alien base: layer 2 is cleared");
    Check((Cell(nFirstCol - 1, nLastRow, 0)->bFlags29 & 0x10) == 0,
          "alien base: one column left of the bound is cleared");
    Check((Cell(nFirstCol, nLastRow - 1, 0)->bFlags29 & 0x10) == 0,
          "alien base: one row up from the last row is cleared");

    /* Any other mission type with layers: the bit is cleared everywhere. */
    Fixture_Reset();
    s_Map.nMapLayers = 2;
    s_Map.nMissionType = 1;
    s_bUnk1511E = 1;
    RunMission(0);
    Check((Cell(30, 50, 0)->bFlags29 & 0x10) == 0,
          "non-alien-base with layers: the bit is cleared");

    /* No layers at all: the bit is set everywhere. */
    Fixture_Reset();
    s_Map.nMapLayers = 0;
    s_Map.nMissionType = 1;
    s_bUnk1511E = 1;
    RunMission(0);
    Check(Cell(0, 0, 0)->bFlags29 & 0x10, "no layers: the bit is set");
    Check(Cell(59, 59, 3)->bFlags29 & 0x10, "no layers: set on every layer");
}

static void TestShowShipReveal(void)
{
    printf("showship reveal\n");
    Fixture_Reset();
    s_Map.nMapLayers = 2;              /* the switch gives (5,5) */
    s_nCraftX = 3;
    s_nCraftY = 3;
    s_bUnk1511E = 1;
    s_Map.nMissionType = 1;            /* layers != 0, so the sweep clears bit 4 first */
    RunMission(0);

    const u8 *pShow = (const u8 *)s_pShowShip;
    if (pShow == NULL) {
        Check(false, "showship.dat was not loaded from the shipped archives");
        return;
    }

    const i32 nEntries = (i32)*(const u16 *)(pShow + 0);
    const i32 nEntryLen = (i32)*(const u16 *)(pShow + 2);
    printf("       showship.dat: %d entries of %d bytes\n", nEntries, nEntryLen);
    Check(nEntries > 0 && nEntryLen > 0, "the header words are count then length");

    const i32 nHalfX = nEntryLen / 2;
    const i32 nHalfY = nEntries / 2;
    /* Again: the terrain pass re-rolled the craft tile, so the reveal is centred on
     * the position it left behind, not on the fixture's 3,3. */
    const i32 nBaseX = 10 * (i32)s_nCraftX;
    const i32 nBaseY = 10 * (i32)s_nCraftY;
    bool bSet = false, bGate = false, bAllLayers = true;

    for (i32 r = 0; r < nEntries && r * nEntryLen + nEntryLen <= 4096; ++r) {
        for (i32 c = 0; c < nEntryLen; ++c) {
            if (pShow[4 + r * nEntryLen + c] == PUFO_SHOWSHIP_NONE) {
                const MapCell *pCell = Cell(nBaseX - nHalfX + c + 5, nBaseY - nHalfY + r + 5, 0);
                if (pCell != NULL && (pCell->bFlags29 & 0x10) != 0) bGate = true;
                continue;
            }
            const i32 nX = nBaseX - nHalfX + c + 5;
            const i32 nY = nBaseY - nHalfY + r + 5;
            if (nX < 0 || nX >= 60 || nY < 0 || nY >= 60) continue;
            bSet = true;
            for (i32 nLayer = 0; nLayer < PUFO_MAP_LAYERS; ++nLayer) {
                const MapCell *pCell = Cell(nX, nY, nLayer);
                if (pCell == NULL || (pCell->bFlags29 & 0x10) == 0) bAllLayers = false;
            }
        }
    }

    Check(bSet, "at least one entry reveals a cell");
    Check(!bGate, "a 0xFF entry reveals nothing");
    Check(bAllLayers, "every revealed column is revealed on all four layers");
}

static void TestBriefing(void)
{
    printf("Cydonia briefing\n");

    Fixture_Reset();

    /* Put known text at the offsets the ARM reads, after the fixture clears the blob. */
    pufo_strcpy((char *)s_aTextBlob + PUFO_BRIEF_B_HEAD, "HEAD");
    pufo_strcpy((char *)s_aTextBlob + PUFO_BRIEF_B_JOIN1, "-MID");
    pufo_strcpy((char *)s_aTextBlob + PUFO_BRIEF_B_JOIN2, "-TAIL1");
    pufo_strcpy((char *)s_aTextBlob + PUFO_BRIEF_A_HEAD, "CYDONIA");
    pufo_strcpy((char *)s_aTextBlob + PUFO_BRIEF_A_JOIN, "-BASE");
    pufo_strcpy((char *)s_aTextBlob + PUFO_BRIEF_A_LINE0, "LINE-ZERO");
    pufo_strcpy((char *)s_aTextBlob + PUFO_BRIEF_TAIL, "LINE-LAST");

    s_bSpawnAlt = 1;
    s_uUiScreen = 7;
    s_Map.nMissionType = 1;
    RunMission(0);

    Check(s_pBriefPad != NULL, "the briefing pad is allocated");
    /* 0x3D29C: LDRH R0,[Map+0x3A]; STR R0,[Map+0x8C]. The save really does read the
     * screen field -- but the prologue at 0x3C64C already cleared it, so on this path
     * the saved value is always zero. The fixture's 7 is gone before the briefing runs. */
    CheckEqI32(s_nUiSlot11, 0, "apUiSlotsA[11] holds the (already cleared) nUiScreen");
    CheckEqI32(s_uUiScreen, 20, "nUiScreen is set to 20");
    CheckEqI32(s_nWrapCalls, 1, "the briefing text is wrapped once");
    CheckEqI32(s_nWrapStart[0], 0, "the non-Cydonia branch wraps from line 0");

    UiPanel *pPanel = s_Env.apPanel[PUFO_BRIEF_PANEL];
    Check(pPanel != NULL, "the briefing panel exists");
    if (pPanel != NULL) {
        Check(pPanel->apLines[0] != NULL &&
              strcmp((const char *)pPanel->apLines[0], "HEAD-MID-TAIL1") == 0,
              "the wrapped text starts at line 0");
        i32 nAppended = pPanel->nLineCount - 2;
        Check(pPanel->apLines[nAppended] != NULL &&
              strcmp((const char *)pPanel->apLines[nAppended], "BRIEF-LINE") == 0,
              "the fixed line is appended after the wrap");
        Check(pPanel->apLines[nAppended + 1] != NULL &&
              strcmp((const char *)pPanel->apLines[nAppended + 1], "LINE-LAST") == 0,
              "the closing slice is appended last");
        /* UiPanel_WrapText @ 0x37144 ends with nLineCount = nLineStart + committed, so the
         * one-line wrap leaves 1; the tail at 0x3D434 then does nLineCount += 2. */
        CheckEqI32(pPanel->nLineCount, 3, "nLineCount is the wrapped line plus the two appended");
    }

    CheckEqI32(s_nSoundInitCalls, 1, "the tactical sample set is loaded once");
    Check(strcmp(s_szSoundPrefix, "tact") == 0, "the sample prefix is \"tact\"");
    CheckEqI32(s_nMusicTrack, 1, "music track 1");
    CheckEqI32(s_nMusicVolCalls, 1, "music volume applied once");
    CheckEqI32(s_nSfxVolCalls, 1, "sfx volume applied once");

    for (i32 i = 0; i < PUFO_TACTINIT_PANELS; ++i) {
        if (s_Env.apPanel[i] != NULL) {
            UiPanel_FreeLines(s_Env.apPanel[i]);
            free(s_Env.apPanel[i]);
            s_Env.apPanel[i] = NULL;
        }
    }
}

static void TestBriefingSkipped(void)
{
    printf("briefing skipped\n");
    Fixture_Reset();
    s_bSpawnAlt = 0;
    RunMission(0);
    Check(s_pBriefPad == NULL, "no pad when the briefing does not run");
    CheckEqI32(s_nWrapCalls, 0, "no wrap when the briefing does not run");
    CheckEqI32(s_uUiScreen, 0, "nUiScreen is left cleared");
}

static void TestSaveModeClearsFlag(void)
{
    printf("save-load mode\n");
    Fixture_Reset();
    s_bUnk254D4 = 1;
    /* nMode 2: the save path without Save_ReadObjBefore. pDat stays NULL, so the save
     * readers fail and leave the grid allocated but empty -- what is asserted here is
     * that unk_138E8C is NOT cleared on this path. */
    RunMission(2);
    CheckEqI32(s_bUnk254D4, 1, "unk_138E8C is left alone when nMode is not 0");
    CheckEqI32(s_bDrawIso, 1, "the shared tail still runs");
    CheckEqI32(s_bSweepArmed, 1, "apUiSlotsA[2] still armed");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *const apszPaths[PUFO_ARCHIVE_COUNT] = { "lang.dat", "main.dat" };
    if (!Asset_OpenArchives(apszPaths)) {
        printf("FAIL: cannot open lang.dat / main.dat\n");
        return 1;
    }

    printf("=== Tact_InitMission @ 0x3c550 ===\n");
    TestPrologueFlags();
    TestWeaponSelAndObjectCopies();
    TestPanelLayout();
    TestDeployCellFlags();
    TestMissionCellFlags();
    TestShowShipReveal();
    TestBriefing();
    TestBriefingSkipped();
    TestSaveModeClearsFlag();

    Asset_CloseArchives();

    if (g_nFailures > 0) {
        printf("\n%d FAILED\n", g_nFailures);
        return 1;
    }
    printf("\nall checks passed\n");
    return 0;
}
