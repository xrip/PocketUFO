/* pufo_tactctl.h - tactical cursor, game speed, and clock control.
 *
 * Evidence: MapCursor_SetFlags @ 0x26f78, MapCursor_IsValid @ 0x275fc,
 *           GameSpeed_Set @ 0x260c0, GameTime_Pause @ 0x5d6ac.
 *
 * ## Cursor field aliasing
 *
 * `MapCursor_*` reach the cursor through `pMap->pad86309a`, a char array IDA
 * names but that actually aliases the surrounding context fields. Resolving the
 * offsets against the `Map` type proves it:
 *
 *     pad86309a[3]  -> 0x15125 + 3  = 0x15128 = nCursorX
 *     pad86309a[7]  -> 0x15125 + 7  = 0x1512c = nCursorY
 *     pad86309a[19] -> 0x15125 + 19 = 0x15138 = nLayerScratch
 *
 * All three land exactly on named fields, so the cursor functions are modelled
 * against those names rather than through the byte aliases.
 */
#ifndef PUFO_TACTCTL_H
#define PUFO_TACTCTL_H

#include "game/pufo_font.h"      /* PufoFont for TactSpawnAliensEnv */
#include "game/pufo_mapcell.h"
#include "game/pufo_objectrec.h"  /* ObjectRecFull for TactDeployEnv */
#include "game/pufo_statusmsg.h" /* StatusStack, StatusHooks for TactSelectCtx */
#include "game/pufo_unit.h"      /* Soldier, UnitCreateEnv for TactDeployEnv */
#include "pufo_common.h"

typedef struct TactUiState TactUiState;

/* The cursor-overlay byte MapCursor_SetFlags writes (nLayerScratch @ 0x15138). */
typedef struct TactCursor {
    i32 nX;            /* nCursorX, 0x15128 */
    i32 nY;            /* nCursorY, 0x1512c */
    u8  nFlags;        /* nLayerScratch, 0x15138, written by SetFlags */
} TactCursor;

/* MapCursor_SetFlags @ 0x26f78. */
void MapCursor_SetFlags(TactCursor *pCursor, u8 nFlags);

/* MapCursor_IsValid @ 0x275fc. In-bounds means 0 <= x < 10*dimX and
 * 0 <= y < 10*dimY. The layer is NOT checked here, unlike Map_IsValidCell. */
bool MapCursor_IsValid(const TactCursor *pCursor, const MapGeom *pMap);

/* GameSpeed_Set @ 0x260c0. Stores the speed and, for values 0..4, restarts the
 * timer set. Speeds outside 0..4 store the value but do not touch the timers --
 * that is the original's behaviour, not a guard. Returns true when the timers
 * were restarted. */
typedef struct GameClock {
    u32  nSpeed;    /* pCtx + 21567 = 0x150fc = nGameSpeed */
    bool bPaused;   /* gUiRootPanel.apLines[106] -> nUnk3032 */
} GameClock;

bool GameSpeed_Set(GameClock *pClock, u32 nSpeed);

/* GameTime_Pause @ 0x5d6ac. */
void GameTime_Pause(GameClock *pClock);

/* The original's timers. GameSpeed_Set kills and restarts them; the port's
 * backend is injected so the speed logic is testable without a window. */
typedef struct GameTimers {
    void (*pfnKillAll)(void *pUser);
    void (*pfnStartAll)(void *pUser);
    void *pUser;
} GameTimers;

void GameSpeed_SetTimers(const GameTimers *pTimers);

/* Speed range the original allows to restart timers. */
#define PUFO_SPEED_MIN 0u
#define PUFO_SPEED_MAX 4u

/* ---------------------------------------------------------------- view maths
 *
 * Five pure coordinate helpers plus the camera-offset writer. All are small and
 * side-effect free, and all read only the map's two dimensions (`nMapDimX` @
 * +86299, `nMapDimY` @ +86300) and the cursor layer, so they take a `MapGeom`
 * rather than the whole context.
 *
 * The original's integer divisions are truncating (ARM SDIV matches C's `/` for
 * non-negative numerators), so a plain C division is exactly equivalent here. The
 * one negative divisor, `(v7 + v5) / -16`, is kept as written.
 */

/* Map_IsoOffsetA/B and Map_CenterOffsetX/Y already live in pufo_mapcell.h with a
 * dimension-argument signature; this module reuses them rather than declaring a
 * second, MapGeom-taking version. */

/* The two scratch bytes MapView_SetCamOffset writes, at pCtx+0x4A40/0x4A41 (both
 * inside the unnamed gap_TrajFront region). */
typedef struct CamOffset {
    u8 bScratch40;   /* 0x4A40 */
    u8 bScratch41;   /* 0x4A41 */
} CamOffset;

/* MapView_SetCamOffset @ 0x276f0.
 *
 * `bAdjustForLayer` is the original's a4: when set, the height is first shifted by
 * `24 * nLayerScratch - 20`. Both outputs are byte writes, so the divisions truncate
 * into a byte rather than wrapping.
 *
 * Note the sign convention on the second byte: the bias is +8 when `a2/2 - a3` is
 * positive and -8 otherwise -- and at exactly 0 the bias is -8, because the test is
 * `v5 <= 0`. The test pins that boundary. */
void MapView_SetCamOffset(const MapGeom *pMap, i32 nA, i32 nB, bool bAdjustForLayer,
                          u8 nLayerScratch, CamOffset *pOut);

/* MapCursor_SetPos @ 0x27014.
 *
 * Runs its body when `!bBusy` or when (nX, nY) is not a valid cell at layer 0.
 * It first clears `bFlags57` bit 3 on the current cursor cell and its large-unit
 * 2x2 footprint across all four layers, then, when (nX, nY) is inside
 * `0 .. 10*dim-1`, stores it and sets the same bits, including the footprint when
 * the selected unit is large AND `bSuppressLarge` (gap_2[1]) is clear. Note the
 * clear phase omits the `bSuppressLarge` condition; only the set phase checks it.
 * Cells are reached through the injected MapCellLookup. */
typedef struct MapCursorPos {
    TactCursor *pCursor;
    MapGeom     geom;
    bool        bBusy;          /* unk_157180->bBusy */
    bool        bSelUnitLarge;  /* Unit_bIsLarge(pSelUnit) */
    bool        bSuppressLarge; /* pMap->gap_2[1] != 0 */
} MapCursorPos;

void MapCursor_SetPos(MapCursorPos *pCtx, i32 nX, i32 nY,
                      MapCellLookup pfnLookup, void *pUser);

/* MapView_EnterLookMode @ 0x163d4.
 *
 * Plays sample 16, sets nUiScreen = 2, centres the camera on half the tile size
 * and derives the two world-offset words from the camera-scroll values at
 * 0x15130/0x15134 (`pad86309a[11]`/`[15]`) plus nTrajFront0[4]/[5] minus 30/40.
 * The sample is injected through `pfnPlaySample`. */
typedef struct LookModeCtx {
    u16 nUiScreen;      /* 0x3a, set to 2 */
    u16 nMapTileW;      /* 0x30 */
    u16 nMapTileH;      /* 0x32 */
    i32 nCamScrollX;    /* 0x15130 */
    i32 nCamScrollY;    /* 0x15134 */
    i32 nTrajFront4;    /* the signed byte at 0x4A40 that SetCamOffset just wrote */
    i32 nTrajFront5;    /* the signed byte at 0x4A41 that SetCamOffset just wrote */
    i32 nCamWorldOff0;  /* 0x4eac */
    i32 nCamWorldOff1;  /* 0x4eb0 */
    u8  nDimX;          /* 0x1511b */
    u8  nDimY;          /* 0x1511c */
    CamOffset camOffset; /* the two scratch bytes SetCamOffset writes */
} LookModeCtx;

void MapView_EnterLookMode(LookModeCtx *pCtx,
                           void (*pfnPlaySample)(u32 nSample, void *pUser),
                           void *pUser);

/* MapView_FocusOnUnit @ 0x2c758. Despite the name the second argument is a
 * MapCell *, not a unit: the callee reads +0x1A/+0x1B/+0x1C, which are
 * MapCell.nX/nY/nZ, and every caller passes a cell (for example the tail call at
 * 0x16744 from MapView_FocusSelected passes Map.pSelUnit->pCell).
 *
 * It recentres the camera on half the tile size with no layer adjustment, then
 * takes the cell coordinate MINUS the two scratch bytes SetCamOffset just wrote,
 * read SIGNED (LDRSB at 0x2C788 and 0x2C7A0), and feeds that pair to
 * Map_CenterOffsetX @ 0x26fcc and Map_CenterOffsetY @ 0x26ffc, storing the results
 * into the camera-scroll pair at 0x15130/0x15134. It ends by tail-calling
 * MapCursor_SetFlags with the cell's own layer plus one, so focusing on a cell on
 * layer 0 puts the cursor on layer 1. */
typedef struct FocusCtx {
    u16 nMapTileW;      /* 0x30 */
    u16 nMapTileH;      /* 0x32 */
    u8  nDimX;          /* 0x1511B */
    u8  nDimY;          /* 0x1511C */
    i32 nCamScrollX;    /* 0x15130, written */
    i32 nCamScrollY;    /* 0x15134, written */
    CamOffset camOffset; /* 0x4A40/0x4A41: written by SetCamOffset, read back signed */
} FocusCtx;

void MapView_FocusOnCell(FocusCtx *pCtx, const MapCell *pCell, TactCursor *pCursor);

/* MapView_FocusSelected @ 0x16710. Sample 17 when the gate is set, then centre on the selected
 * unit's cell. The gate is the low byte of Map.apUiSlotsA[2], passed in because the port does not
 * own that slot array. */
void MapView_FocusSelected(FocusCtx *pCtx, const MapCell *pSelCell, u8 bGateByte,
                           TactCursor *pCursor);

/* Tact_GetCameraBounds @ 0x3331c.
 *
 * When `bUseCraft` (byte 0x1A21 / gap_6556[133]) is clear the whole map is
 * returned: (0,0) to (10*dimX, 10*dimY). For the alien-base mission type
 * (nMissionType == 4) it returns the bottom three-tile strip: X from
 * 10*(dimX-3) to that plus 30, Y from 10*dimY-10 to 10*dimY. Otherwise it centres
 * on the craft's map tile and widens the box by craft type: type 1 adds 20 to X
 * and 10 to Y, types 0 and 2 add 10 to X and 20 to Y, and other types leave the
 * box at the craft tile. */
typedef struct CameraBoundsCtx {
    bool bUseCraft;    /* gap_6556[133] @ 0x1A21 != 0 */
    u8   nMissionType; /* 0x1511A */
    u8   nMapDimX;     /* 0x1511B */
    u8   nMapDimY;     /* 0x1511C */
    u8   nCraftMapX;   /* 0x15139 */
    u8   nCraftMapY;   /* 0x1513A */
    u8   bCraftType;   /* apCraft[nDeployCraftIdx]->bCraftType */
} CameraBoundsCtx;

void Tact_GetCameraBounds(const CameraBoundsCtx *pCtx, i32 *pnX0, i32 *pnY0,
                          i32 *pnX1, i32 *pnY1);

/* --------------------------------------------------- facing scratch and tiles
 *
 * Map_InitFacingScratch @ 0x26f14 maintains a small view scratch block. Field
 * offsets, resolved against `Map`:
 *
 *     nMapTileW  @ 0x30 (u16)     nMapTileH  @ 0x32 (u16)
 *     gap_52[0]  @ 0x34           gap_52[1]  @ 0x35
 *     gap_52[2]  @ 0x36  <- the stored facing byte it compares against
 *     bTactFlag  @ 0x00
 *
 * The whole body is guarded on the facing CHANGING, so calling it twice with the
 * same facing does nothing the second time. When the facing is new AND the map is
 * tactical, it resets the tile size to 240x320 and recomputes two scratch bytes:
 *
 *     gap_52[0] = 9
 *     gap_52[1] = 24 * nLayerScratch / 16 + 47
 *
 * Note `24*x/16` is NOT `1.5*x` in integer arithmetic for every x, because the
 * division truncates: 24*1/16 = 1 while 3*1/2 = 1 (equal here), but 24*3/16 = 4
 * while 3*3/2 = 4 (also equal). They differ only for x where 24x is not divisible
 * by 16 and the halving would round differently; the port keeps the original's
 * operation order exactly rather than simplifying to a multiply. */
typedef struct MapFacingScratch {
    u8 bStoredFacing;   /* gap_52[2] @ 0x36 */
    u8 bScratch34;      /* gap_52[0] @ 0x34 */
    u8 bScratch35;      /* gap_52[1] @ 0x35 */
} MapFacingScratch;

typedef struct MapTileSize {
    u16 nTileW;   /* 0x30 */
    u16 nTileH;   /* 0x32 */
} MapTileSize;

/* The tile size the tactical branch resets to. */
#define PUFO_TILE_W 240u
#define PUFO_TILE_H 320u

/* Returns true when the scratch was updated (the facing changed). */
bool Map_InitFacingScratch(MapFacingScratch *pScratch, MapTileSize *pSize, u8 nFacing,
                          bool bTactFlag, u8 nLayerScratch);

/* Tact_ScanVictoryTiles @ 0x33264.
 *
 * A victory condition for the alien-base mission type. The gate at 0x3328C is
 * `Map_bSpawnAlt != 0 && Map_nMissionType == MissionType_AlienBase`; the scan then walks a 2x2
 * block of cell slots starting at the mission spawn tile, `Map_nSpawnTileY * 300 +
 * Map_nSpawnTileX * 5` dwords into the cell array (0x332C0), two slots per tile-row (0x332E4) and
 * 30 slots per row step (0x332F8), and reports victory as soon as one of those cells has an empty
 * occupied-object list (`+12 == 0` at 0x332E0).
 *
 * The scan is addressing-heavy; the port takes an injected 2x2 probe so the predicate can be
 * tested without the grid. `pfnCellHasNoItems(nSpawnTileX + dx, nSpawnTileY + dy)` must return
 * true when that cell has no object at its list head. The four probes run in the original's
 * order: (0,0), (1,0), then (0,1), (1,1) -- columns within a row, then the next row.
 *
 * The parameters were first named bCraftTileA0NonZero / nCraftTileX / nCraftTileY, which read as
 * though the scan started at the deployed craft. The IDB names on the three addresses it actually
 * reads -- Map_bSpawnAlt @ 0x128B00, Map_nSpawnTileX @ 0x128B04, Map_nSpawnTileY @ 0x128B08 -- say
 * otherwise, so the names follow the addresses. */
bool Tact_ScanVictoryTiles(bool bSpawnAlt, u8 nMissionType, i32 nSpawnTileX,
                           i32 nSpawnTileY,
                           bool (*pfnCellHasNoItems)(i32 nX, i32 nY, void *pUser),
                           void *pUser);

/* The mission type the victory scan applies to. */
#define PUFO_MISSION_TYPE_ALIEN_BASE 4u

/* ------------------------------------------------- the deploy confirm dialog */

/* Map_BuildDeployConfirmYesNo @ 0x18a68, the widget-56 handler of the deploy panel.
 *
 * It counts units inside and outside the craft rectangle -- the SAME rectangle
 * Tact_GetCameraBounds computes, reached here with bUseCraft always true (the
 * original has no whole-map branch) -- and asks YES/NO.
 *
 * The classification has four buckets and a hole:
 *
 *     nUnitType < 4    inside -> nSmallIn   outside -> nSmallOut
 *     4 <= nUnitType <= 8  inside -> nBigIn  outside -> nBigOut
 *     nUnitType > 8    NOT COUNTED AT ALL
 *
 * The rect test is inclusive on all four edges. Civilians are skipped entirely when
 * `uHp @ 0x00A` is zero; their position comes from MapCell.nX/nY @ 0x1A/0x1B and
 * their type from Soldier.nUnitType @ 0x0A0, exactly like the XCom pass.
 *
 * The two messages are the original's scratch buffers unk_E90D9 and unk_E9273, built
 * as `itoa(count) + text 28 + itoa(count) + text 29` and then a copy of text record
 * 30 for the first one. The second copies unk_E900C instead, which has no writer
 * anywhere in the executable (xrefs_to 0xE900C returns only the two uses inside this
 * function) and lies past the file-backed .data end, so its tail is empty. */
#define PUFO_TXTREC_DEPLOY_IN   28
#define PUFO_TXTREC_DEPLOY_JOIN 29
#define PUFO_TXTREC_DEPLOY_TAIL 30

typedef struct DeployCounts {
    i32 nSmallIn;
    i32 nSmallOut;
    i32 nBigIn;
    i32 nBigOut;
} DeployCounts;

typedef struct DeployCountCtx {
    CameraBoundsCtx Bounds;   /* bUseCraft is ignored: the rect is always the craft one */
    Soldier *const *apXcom;   /* Map.apXcomUnits @ 0x15154 */
    i32 nXcomCount;           /* Map.nXcomUnitCount @ 0x1511F */
    Soldier *const *apCiv;    /* Map.apCivilians @ 0x15244 */
    i32 nCivCount;            /* Map.nCivilianUnitCount @ 0x15120 */
} DeployCountCtx;

/* The counting half on its own, so the buckets can be tested without the UI. */
void Tact_CountDeployUnits(const DeployCountCtx *pCtx, DeployCounts *pOut);

typedef struct DeployConfirmHooks {
    /* Sound_Play @ 0x9e0c0, sample 16 before the modal. */
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    /* UiModal_Begin @ 0x26430 at 170,1069 with five buttons. The original passes
     * "YES" @ 0xA62E8 with j_Tact_CheckBattleEnd (0x18a44) and "NO" @ 0xA62EC with
     * Map_ClearUiScreen @ 0x18a54, ported as Ui_ClearScreen. */
    void (*pfnModal)(i32 nX, i32 nY, i32 nCount, const char *szYes, const char *szNo,
                     void *pUser);
    /* aTextRecs @ 0xB38D0, 205-byte records. NULL is read as an empty fragment. */
    const char *(*pfnTextRecord)(i32 nRecord, void *pUser);
    void *pUser;
} DeployConfirmHooks;

typedef struct DeployConfirmCtx {
    DeployCountCtx     Count;
    TactUiState       *pUi;      /* nUiScreen @ 0x3A is set to 9 */
    char              *aszMsg1;  /* unk_E90D9 */
    char              *aszMsg2;  /* unk_E9273 */
    i32                nMsgCap;
    const DeployConfirmHooks *pHooks;
} DeployConfirmCtx;

/* Returns 9, the screen id the original returns and stores in nUiScreen. */
i32 Map_BuildDeployConfirmYesNo(const DeployConfirmCtx *pCtx);
/* The full mission-type enum, the immediates Smap_BeginCraftSelect @ 0x83CB8 stores for crash
 * tiles 240..244, cross-checked against g_aTerrainModuleCount's index order (jungle 12, culta 19,
 * forest 12, xbase 21, ubase 16, urban 15, desert 12, mount 13, polar 14, mars 11). */
#define PUFO_MISSION_TYPE_JUNGLE   0u   /* MissionType_Jungle */
#define PUFO_MISSION_TYPE_CULTA    1u   /* MissionType_Culta */
#define PUFO_MISSION_TYPE_FOREST   2u   /* MissionType_Forest */
#define PUFO_MISSION_TYPE_DESERT   6u   /* MissionType_Desert */
#define PUFO_MISSION_TYPE_MOUNTAIN 7u   /* MissionType_Mountain, no writer found */
#define PUFO_MISSION_TYPE_POLAR    8u   /* MissionType_Polar */

/* --- selecting a unit -------------------------------------------------------------
 *
 * Tact_SelectUnit @ 0x2c6c4.
 *
 * One Map field is involved, read twice and then written. Hex-Rays renders the second and third uses
 * as `*((_DWORD *)pCtx + 21709)`, which is Map+0x15354 - thirty-two bytes past the first use. That is a
 * Hex-Rays artifact. The ARM is `MOV R3, #0x15334` followed by the pre-indexed `LDR R3, [R5, R3]!`, so
 * R5 ends up pointing at Map+0x15334 and the following `STR R4, [R5]` writes that same slot. Both
 * immediates decode to 0x15334 (0xE3A0CA15 + 0xE38CCFCD is 0x15000 + 0x334), and Map_pSelUnit is
 * 0x1139B8 + 0x15334.
 *
 *   if pSelUnit != pUnit and pUnit is operative, push Unit_FormatDisplayName(pUnit, 0) as a status
 *     message, not forced
 *   if pSelUnit is non-null, park the cursor at (-2, -2)
 *   store pUnit into pSelUnit
 *   move the cursor onto pUnit's cell
 *
 * So re-selecting the same unit pushes nothing but still parks and re-stores; selecting an inoperative
 * unit pushes nothing but still parks, stores and moves; and the first selection in a mission does not
 * park the cursor, because the slot was null.
 *
 * Deviation: the original returns whatever MapCursor_SetPos leaves in R0. The port returns void. */
typedef struct TactSelectCtx {
    Soldier          **ppSelUnit;     /* &Map.pSelUnit @ 0x15334: one slot, read twice then written */
    MapCursorPos      *pCursorPos;
    MapCellLookup      pfnLookup;
    void              *pLookupUser;
    StatusStack       *pStatus;
    const StatusHooks *pStatusHooks;
    const char        *panRankLow;   /* aRankNameLow @ 0xB2200 */
    const char        *panRankOther; /* aRankNameOther @ 0xB2148 */
} TactSelectCtx;

void Tact_SelectUnit(TactSelectCtx *pCtx, Soldier *pUnit);

/* Tact_SelectTeamUnits @ 0x39744.
 *
 * Builds the deploy anchor, then walks the soldier array and decides who joins the tactical roster.
 *
 * The anchor comes from aDeployCellOffsets @ 0xA66B8, records of 104 bytes indexed by the mission layer
 * count. Record byte 0 is added to 10 * Map_nCraftMapX @ 0x15199 and byte 1 to 10 * Map_nCraftMapY @
 * 0x1519A; byte 2 is the layer. The craft position is held in tiles and 10 is cells per tile, which is the
 * same factor Tact_GetCameraBounds (0x3331c) applies to those same two bytes. A layer count of zero puts
 * the anchor at (0, 0, 0) without reading the table at all.
 *
 * Soldier+0x1B4 (nUnk436) is cleared for every soldier visited, before any acceptance test, so a rejected
 * soldier still has it cleared.
 *
 * Map_bMissionMode @ 0x1511D picks the rule:
 *
 *   0, 2, 3   accept when Soldier.bUnk183 == Map_nCraftIndex @ 0x15113, the craft the mission launched from
 *   1         accept when Soldier.nKind == the selected base byte unk_159D81, and bUnk183 is not 254, and
 *             either bUnk183 is 255 (aboard nothing) or apCraft[bUnk183]->bAtBase is set
 *   above 3   accept unconditionally
 *
 * Mode 1 is the base-deploy case and it is the only branch that reaches apCraft (Map+0x10AC). Hex-Rays
 * renders the base byte as MEMORY[0x158890].anLineAux[79]; the ARM loads unk_159D81, the byte immediately
 * after the base count at 0x159D80, and it loads it with LDRSB. That signed load is reproduced here: a base
 * byte of 0x80 or more becomes a negative int and then matches no nKind, which an unsigned compare would
 * not do. In practice base indices are small, so the quirk is latent.
 *
 * The behaviour most likely to be ported wrong: a rejected soldier ENDS the walk rather than being skipped.
 * The rejection branch jumps straight to the count read, so one unsuitable soldier leaves every later
 * soldier unprocessed.
 *
 * Accepted soldiers get Unit_ResetBattleState with facing 0, bStatus = 0, Unit_PlaceOnMap at the anchor, and
 * a slot in apXcomUnits @ 0x15154. If the roster is not empty the first entry is selected through
 * Tact_SelectUnit, and then every entry has its inventory-derived stats recalculated.
 *
 * Deviation: the original returns the count through the loop's register chain. The port returns it as i32. */
#define PUFO_DEPLOY_RECORD_SIZE 104

typedef struct TactTeamSelectEnv {
    Soldier **apSoldiers;          /* the soldier array */
    i32      nSoldierCount;        /* its length; the original reads the count as a u16 */
    struct Ship **apCraft;         /* Map.apCraft @ Map+0x10AC, only mode 1 touches it */
    i32      bMissionMode;         /* Map_bMissionMode @ 0x1511D */
    u8       nCraftIndex;          /* Map_nCraftIndex @ 0x15113 */
    u8       nMapLayers;           /* Map.nMapLayers @ 0x15114, the record index */
    u8       nCraftMapX;           /* Map_nCraftMapX @ 0x15199, in tiles */
    u8       nCraftMapY;           /* Map_nCraftMapY @ 0x1519A, in tiles */
    i8       nSelectedBase;        /* unk_159D81, signed because the original loads it with LDRSB */
    const u8 *pDeployOffsets;      /* aDeployCellOffsets @ 0xA66B8 */
    Soldier **apXcomUnits;         /* Map.apXcomUnits @ 0x15154 */
    u8      *pnXcomCount;          /* nXcomUnitCount @ 0x1511F */
    const u16 *auStat;             /* for Unit_ResetBattleState */
    i32      nStatCount;
    MapCellLookup      pfnLookup;  /* for Unit_PlaceOnMap */
    void              *pLookupUser;
    TactSelectCtx     *pSelectCtx; /* for Tact_SelectUnit */
    const struct InvStatHooks *pInvHooks; /* for Unit_RecalcInventoryStat */
} TactTeamSelectEnv;

i32 Tact_SelectTeamUnits(TactTeamSelectEnv *pEnv);

/* ---------------------------------------------------- deploying the team and hardware soldiers */

/* MissionType_XcomBase. ARM at 0x3A058 is CMP R2, #3 on Map_nMissionType. */
#define PUFO_MISSION_TYPE_XCOM_BASE 3u

/* MissionType_AlienBase. ARM at 0x33750 (the tail of Tact_TallyBattleStats) is CMP R3, #4 on
 * Map_nMissionType, and at 0x344B4 Tact_TallyCraftRecovery tests the same field against 3 for the
 * Xcom base defence. */
#define PUFO_MISSION_TYPE_ALIEN_BASE 4u

/* The craft store slots: one object record per hardware type the launching craft carries.
 * Tact_DeployTeamAndHwps spawns soldiers from them (the unit type is the object index minus 27, so
 * 31..35 give types 4..8), and Tact_SetupCraftCargo skips them in both scratch walks because they
 * are cargo, not equipment. */
#define PUFO_CRAFT_STORE_FIRST 31
#define PUFO_CRAFT_STORE_LAST  35

/* The tile type that seeds a scatter cluster in the phase-5 table build. ARM at 0x3A098 is
 * CMP against 7 over unk_159D5C. */
#define PUFO_SCATTER_TILE_TYPE 7

struct RmpData; /* game/pufo_rmp.h */

/* Everything Tact_DeployTeamAndHwps reaches through the Map block. */
typedef struct TactDeployEnv {
    /* apSoldiers is Map+0x1198 and the count is the u16 at Map+0x1998. */
    UnitCreateEnv   create;
    Soldier       **apXcomUnits;         /* Map.apXcomUnits @ Map+0x15154 */
    u8             *pnXcomUnitCount;     /* nXcomUnitCount @ Map+0x1511F, a byte in the original */
    TactItem      **apTactItems;         /* Map.apTactItems @ Map+0x23438 */
    i32            *pnTactItemCount;     /* Map_nTactItemCount @ Map+0x1513C */
    i32            *pnDeployedHwps;      /* unk_128AF8 @ Map+0x15140, the running hardware count */
    ObjectRecFull **apObjects;           /* Map.apObjects @ Map+0x108 */
    MapCellLookup   pfnLookupCell;
    void           *pLookupUser;
    i32             nMapLayers;          /* Map.nMapLayers @ Map+0x15114 */
    u8              nCraftMapX;          /* Map_nCraftMapX @ Map+0x15199, in tiles */
    u8              nCraftMapY;          /* Map_nCraftMapY @ Map+0x1519A, in tiles */
    u8              nMissionType;        /* Map_nMissionType @ Map+0x1511A */
    const u8       *pDeployOffsets;      /* aDeployCellOffsets @ 0xA66B8, 104-byte records */
    const u8       *pDeployPointCounts;  /* aDeployPointCounts @ 0xA67F0: 14, 12, 26 */
    const u8       *pHwpsOffsets;        /* aHwpsDeployOffsets @ 0xA6688, twelve 4-byte records */
    struct RmpData *pRmp;                /* unk_15881C, used when nMapLayers is 0 */
    TactSelectCtx  *pSelect;             /* phase 4 selects apXcomUnits[0] */

    /* Phase 5 scatter tables, only touched when nMapLayers is 0. The original indexes its cell
     * array as 60 * (60 * layer + nY) + nX, so the table named Mid feeds the middle index and the
     * table named Last feeds the final one. */
    i32            *panScatterMid;       /* unk_1556D0 */
    i32            *panScatterLast;      /* unk_155FD0 */
    i32            *pnScatterCount;      /* unk_157200 */
    const u8       *pTileTypes;          /* unk_159D5C, 36 tile bytes in 6 rows of 6 */
} TactDeployEnv;

/* Tact_DeployTeamAndHwps @ 0x39980. Put the team into the mission and spawn the hardware soldiers
 * that came with the craft.
 *
 * Five phases:
 *
 *   1. Park every XCom unit already in the list at cell (1,1,0).
 *   2. For object indices 31..35, create apObjects[i]->uRecovScore soldiers, give each the stat
 *      preset for its object, name it after the object, place it on a hardware deploy point, and
 *      hand it one TactItem carrying that object.
 *   3. Re-place the units parked in phase 1, scanning deploy points for the first cell with no
 *      occupant. The bound is nXcomUnitCount minus the deployed count, so only the pre-existing
 *      team is re-placed.
 *   4. Select apXcomUnits[0].
 *   5. Only when nMapLayers is 0: build a scatter table and move every tactical item off cell
 *      (0,0,0) onto layer-1 cells, cycling through the table.
 *
 * The number of soldiers is read from ObjectRec.uRecovScore at 0xCA, not uRecovCount at 0xCC.
 * ARM at 0x39A24 is LDRH R11, [R3,#0xCA]. */
i32 Tact_DeployTeamAndHwps(TactDeployEnv *pEnv);

/* ---------------------------------------------------- spawning the alien squad */

/* Mission types Tact_SpawnAliens branches on. Values proven from ARM compares:
 * CMP #5 and CMP #3 at 0x3AC00 (Urban / XcomBase), CMP #9 at 0x3A338 (MarsOrSpecial),
 * CMP #4 and CMP #9 at 0x3A41C (AlienBase / MarsOrSpecial). */
#define PUFO_MISSION_TYPE_URBAN         5u
#define PUFO_MISSION_TYPE_MARS_SPECIAL  9u

/* The 32-byte alien stat record aAlienUnitStats @ 0xa8f38 carries, index nUnitType - 9.
 * Field map taken from ARM at 0x3A934..0x3AA40. Sixteen halfwords, no padding. */
typedef struct AlienUnitStatRec {
    i16 nTu;        /* +00 -> auStat[0] and uTu */
    i16 nStat8;     /* +02 -> auStat[8], plus a rand(20) - 10 jitter */
    i16 nStat9;     /* +04 -> auStat[9] */
    i16 nStat2;     /* +06 -> auStat[2], plus a jitter */
    i16 nHp;        /* +08 -> auStat[1], plus a jitter */
    i16 nStat3;     /* +0A -> auStat[3], plus a jitter */
    i16 nStat6;     /* +0C -> auStat[6]. -1 means "roll both": auStat[6] and auStat[7]
                     * each become rand(50) + 30 and bUnk422 is set to 1. */
    i16 nStat7;     /* +0E -> auStat[7] */
    i16 nUnk248;    /* +10, low byte -> bUnk248 */
    i16 nReady[5];  /* +12, +14, +16, +18, +1A, low bytes -> abReady252[0..4] */
    i16 nStat4;     /* +1C -> auStat[4] */
    i16 nStat5;     /* +1E -> auStat[5] */
} AlienUnitStatRec;
_Static_assert(sizeof(AlienUnitStatRec) == 32, "aAlienUnitStats records are 32 bytes");

/* The sprite-set name table aXcom0 @ 0xa7be8: 23 nine-byte names, index nUnitType. */
#define PUFO_ALIEN_SPRITE_SET_NAME_COUNT 23

/* Everything Tact_SpawnAliens reaches through the Map block, the font block and the
 * mission data.
 *
 * Two of the original's inputs are stack slots it never writes, and a C port cannot read
 * uninitialised memory, so both come in as fields and both are recorded in
 * docs/parity/bugs.tsv:
 *
 *   nFourthRaceCount  - var_24, read at 0x3A8BC as the fourth race group's count. The whole
 *                       function has exactly two references to var_24: this read and the frame
 *                       declaration. It bounds the group that gets Map_abSpriteSet[3].
 *   nAlienSetIndex    - var_48. Written for nMissionMode 0, 1, 2 and 3 (0x3A31C, 0x3A30C,
 *                       0x3A304, 0x3A2F4) and overwritten when nMissionType is 9, but the
 *                       `BNE loc_3A324` at 0x3A2F0 leaves it unwritten for any mode above 3.
 *                       The port computes it for modes 0..3 and uses this field only above 3.
 */
typedef struct TactSpawnAliensEnv {
    UnitCreateEnv    create;              /* apSoldiers @ Map+0x1198, count @ Map+0x1998 */
    Soldier        **apEnemyUnits;        /* unk_128B84 @ Map+0x151CC */
    u8              *pnEnemyUnitCount;    /* nEnemyUnitCount @ Map+0x15124, a byte in the original */
    Soldier        **apHiddenSoldiers;    /* apHiddenSoldiers @ Map+0x25438 */
    i32             *pnEnemyReserveCount; /* Map_nEnemyReserveCount @ Map+0x15144 */
    TactItem       **apTactItems;         /* Map.apTactItems @ Map+0x23438 */
    i32             *pnTactItemCount;     /* Map_nTactItemCount @ Map+0x1513C */
    ObjectRecFull  **apObjects;           /* Map.apObjects @ Map+0x108; [11] is Map_apObjects11 */
    PufoFont        *pFont;               /* fontSys @ 0x158268 */
    struct RmpData  *pRmp;                /* pRmpMissionData @ 0x15881C */
    MapCellLookup    pfnLookupCell;       /* for Rmp_PickSpawnForUnit */
    void            *pLookupUser;
    const InvStatHooks *pInvHooks;        /* for Unit_RecalcInventoryStat */
    /* Tact_KillUnit (0x4e430) at the tail. A seam, not a call: the kill path needs the whole
     * KillCtx, which belongs to the combat layer. */
    void (*pfnKillUnit)(Soldier *pUnit, void *pUser);
    void            *pKillUser;

    u8   nMissionMode;       /* Map_bMissionMode @ Map+0x1511D */
    u8   nMissionType;       /* Map_nMissionType @ Map+0x1511A */
    u8   nTerrainVariant;    /* Map_nTerrainVariant @ Map+0x15111 */
    u8   nAlienTier;         /* Map_bUnk15112 @ Map+0x15112, the column within a terrain row */
    u8   nAlienPoolScale;    /* Map_bUnk15115 @ Map+0x15115, the stealth-budget scale */
    u8   abSpriteSet[4];     /* Map_abSpriteSet @ Map+0x15116..0x15119, 23 means "no race" */
    u16  nAlienPoolAddend;   /* nAlienPoolAddend @ Map+0x33910, read with LDRH */
    const char *panUnitName; /* 0xB1140, 40-byte records indexed by nUnitType (.bss, loaded at run
                              * time, so it is injected rather than dumped) */

    i32  nAlienSetIndex;     /* used only when nMissionMode is above 3; see the note above */
    i32  nFourthRaceCount;   /* the original never writes it; see the note above */
} TactSpawnAliensEnv;

/* Tact_SpawnAliens @ 0x3a2ac. Build the alien squad for the mission.
 *
 * Nine steps, in the original's order:
 *
 *   1. Pick the alien set index from nMissionMode (0 -> nTerrainVariant - 1, 1 -> 7, 2 -> 6,
 *      3 -> 5) and force it to 5 when nMissionType is MarsOrSpecial. Seed the eight-slot
 *      composition array from g_aAlienRaceComposition[5 * index].
 *   2. Decide the three group sizes. Urban, XcomBase, AlienBase and MarsOrSpecial use fixed
 *      formulas with their own RNG order; every other type reads g_aAlienRaceCounts and
 *      g_aAlienSecondaryCounts at 3 * nTerrainVariant + nAlienTier - 3. A lead race of 10 or
 *      12 forces the secondary count to 0.
 *   3. Split the primary count into the first three race groups. The 0.8 scaling is skipped
 *      when the terrain is shallow or when both secondary races are large.
 *   4. Compute the stealth budget: (nAlienPoolScale - 20) * primary * 0.01, reduced by one when
 *      it would equal the whole primary count.
 *   5. Spawn primary + secondary + reserve units, assign each a race by position, copy its name
 *      from panUnitName, roll its stats from g_aAlienUnitStats, reset its battle state, flag up
 *      to the stealth budget of them, recalc its burden and place it through the rmp spawn
 *      picker - except for the XCom base defence mission, where the units go into
 *      apHiddenSoldiers / nEnemyReserveCount and are not placed at all.
 *   6. Load the four sprite sets into the font and rebuild the index.
 *   7. Recompute the eight-slot composition as
 *      (g_aAlienMissionTotals[index] + nAlienPoolAddend) / g_aAlienCompositionDivisor[i].
 *   8. For every unit whose type table byte +2 is set, walk g_aAlienBeltObjects backwards and
 *      give it a hand weapon and then a belt item, each drawn from the composition counts.
 *   9. Kill every flagged unit outright, restarting the scan from index 0 after each kill.
 *
 * Deviation: the original returns loop-counter residue. The port returns the number of units
 * spawned into apEnemyUnits. */
i32 Tact_SpawnAliens(TactSpawnAliensEnv *pEnv);

/* ---------------------------------------------------- pre-mission craft cargo */

/* The two scratch blocks Tact_SetupCraftCargo works through. Both are .bss in the original and
 * neither has any other reader, but the function does not clear either one wholesale: it zeroes one
 * entry per object it actually processes, so an object it skips keeps whatever an earlier call left.
 * The caller owns the buffers and passes the same pair every call, which is what the original's
 * .bss residency means.
 *
 *   unk_156CD0  one i32 per object: the running ammo split.
 *   unk_157208  three i32 per object: all three hold the same value once the object's pass ends,
 *               and only the first two are ever read back. */
#define PUFO_CARGO_WORK_PER_OBJ 3

/* The object ranges the function treats as stores rather than as equipment. The craft store slots
 * are PUFO_CRAFT_STORE_FIRST..LAST; 56, 57, 58 and 105 are the base store records, and 105 is the
 * one the XCom base tail works on. 34 is the seed record whose base column is captured on entry. */
#define PUFO_CARGO_BASE_FIRST   56
#define PUFO_CARGO_BASE_LAST    58
#define PUFO_CARGO_SEED_OBJECT  34
#define PUFO_CARGO_BASE_OBJECT  105

/* Everything Tact_SetupCraftCargo reaches through the Map block. */
typedef struct TactCargoEnv {
    ObjectRecFull   **apObjects;       /* Map.apObjects @ Map+0x108 */
    u16              *pnObjectCount;   /* Map_nObjectCount @ Map+0x10A8 */
    struct Ship     **apCraft;         /* Map.apCraft @ Map+0x10AC */
    u8               *pnCraftCount;    /* Map_nCraftCount @ Map+0x1194 */
    TactItem        **apTactItems;     /* Map.apTactItems @ Map+0x23438 */
    i32              *pnTactItemCount; /* Map_nTactItemCount @ Map+0x1513C */
    MapCell         **ppDeployCell;    /* 0x157204. Written here, read by
                                        * Tact_EquipTeamFromCraft (0x3be24). */
    i32              *panCargoSplit;   /* unk_156CD0, one entry per object */
    i32              *panCargoWork;    /* unk_157208, PUFO_CARGO_WORK_PER_OBJ per object */
    MapCellLookup     pfnLookupCell;
    void             *pLookupUser;
    const u8         *pDeployOffsets;  /* aDeployCellOffsets @ 0xA66B8, 104-byte records */
    u8   nMissionType;                 /* Map_nMissionType @ Map+0x1511A */
    u8   nCraftIndex;                  /* Map_nCraftIndex @ Map+0x15113, the launching craft */
    u8   nMapLayers;                   /* Map_nMapLayers @ Map+0x15114, the record index */
    u8   nCraftMapX;                   /* Map_nCraftMapX @ Map+0x15199, in tiles */
    u8   nCraftMapY;                   /* Map_nCraftMapY @ Map+0x1519A, in tiles */
    i8   nBaseIndex;                   /* 0x158848, loaded with LDRSB */
} TactCargoEnv;

/* Tact_SetupCraftCargo @ 0x38f00. Turn the cargo the launching craft (or the mission base) carries
 * into the tactical item list, before any unit is deployed. Called once, from Tact_InitMission
 * (0x3c550).
 *
 * Nine steps, in the order the ARM runs them:
 *
 *   1. Capture apObjects[34]->auPerBaseVal[nBaseIndex] on entry. Step 9 needs it as it was before
 *      anything moved, which is why the arithmetic there looks circular.
 *   2. Resolve the deploy cell from the mission layer record and store it at 0x157204. The index is
 *      3600*layer + 60*(10*nCraftMapY + rec[1]) + (10*nCraftMapX + rec[0]) over the cell array at
 *      Map+0x15338, so it is the same anchor Tact_SelectTeamUnits builds. A layer count of zero
 *      reads cell (0, 0, 0).
 *   3. Not the XCom base mission: every object's uRecovScore becomes its own column in the
 *      launching craft's aItemQty. That is the whole cargo pass.
 *   4. XCom base mission: walk objects and STOP at the first one whose uSortKey is 59 or more --
 *      the walk ends, it does not skip. Each visited object's score is cleared first; then, for
 *      every object that passes the tally filter, the score takes the mission base's column and
 *      that column is zeroed. Cargo moves off the base and onto the mission.
 *   5. XCom base mission, second pass: every craft whose bHomeBase is the mission base and that is
 *      actually at base gets bWeaponsHot = 1, and its own aItemQty column is added into every
 *      filtered object's score. This pass has no uSortKey stop.
 *   6. Ammo split. For each object outside the craft store slots, with a finite ammo cap, a
 *      non-zero score and bTwoHanded == 0, walk its three ammo type ids and rebalance the running
 *      split against the ammo record's own score, then store the split into all three slots of the
 *      object's work triple.
 *   7. Item build. For each object outside the store slots whose score exceeds its split, create
 *      exactly score - split TactItems at the deploy cell, each with pObj set and inserted by
 *      priority. While the counter is below the split the item is a weapon: its cap comes from the
 *      ammo record the work triple selects and its index is that ammo type id. Once the counter
 *      reaches the split the item is the ammo itself: its cap is the object's own and its index is
 *      the object index.
 *   8. Any item whose object has bTwoHanded == 1 takes its cap from the object, overriding step 7.
 *   9. Hand the launching craft's four cargo counts to the base store records -- 56, 57, 105, 58,
 *      with 105 taking cargo[2] / 10 -- and zero each count. For the XCom base mission, object 105
 *      also takes back the base column into its score, and the column is then reduced by five times
 *      the value object 34 held on entry, clamped at zero rather than wrapping.
 *
 * The tally filter used by steps 4 and 5: an object joins unless bTwoHanded is 2, except the craft
 * store slots 31..35 and the base store records 56..58, which always join.
 *
 * Deviation: the original's return register is residue -- Map_nMissionType on the non-XCom-base
 * path and the recomputed base column on the XCom-base path. Its only caller ignores it, so the
 * port returns void. */
void Tact_SetupCraftCargo(TactCargoEnv *pEnv);

#endif /* PUFO_TACTCTL_H */
