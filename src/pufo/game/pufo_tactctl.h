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

#include "game/pufo_mapcell.h"
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
 * A victory condition for the alien-base mission type: it probes a 2x2 block of
 * cells starting at the craft tile (X, Y) and reports victory when any of those
 * cells' occupied-object list is empty. The scan is addressing-heavy; the port
 * takes an injected 2x2 probe so the predicate can be tested without the grid.
 *
 * `pfnCellHasNoItems(craftX + dx, craftY + dy)` must return true when that cell has
 * no object at its list head. The four probes run in the original's order: (0,0),
 * (1,0), then (0,1), (1,1) -- i.e. columns within a row, then the next row. */
bool Tact_ScanVictoryTiles(bool bCraftTileA0NonZero, u8 nMissionType, i32 nCraftTileX,
                           i32 nCraftTileY,
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
#define PUFO_MISSION_TYPE_FOREST 2u   /* MissionType_Forest */
#define PUFO_MISSION_TYPE_DESERT   6u   /* MissionType_Desert */

#endif /* PUFO_TACTCTL_H */
