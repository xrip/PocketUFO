/* test_tactctl.c - checks cursor bounds and game-speed/timer control.
 *
 * The cursor field aliasing is the thing worth pinning: MapCursor_* reach their
 * fields through a char array that IDA names pad86309a, and those offsets resolve
 * onto named Map fields. The test asserts the resolved behaviour, not the alias.
 */
#include "game/pufo_tactctl.h"
#include "game/pufo_tactui.h"
#include "game/pufo_unit.h"
#include "game/pufo_ship.h"   /* Ship.bAtBase for the mode 1 rule */
#include "util/pufo_str.h" /* pufo_strcpy: strcpy is deprecated under /WX */

#include <stdio.h>
#include <string.h>

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
        printf("  ok   %s (%d)\n", pszWhat, (int)nGot);
    } else {
        printf("  FAIL %s: got %d, want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

/* --- timer recording --- */

static int g_nKill;
static int g_nStart;

static void Fake_KillAll(void *pUser) { (void)pUser; g_nKill += 1; }
static void Fake_StartAll(void *pUser) { (void)pUser; g_nStart += 1; }

static void TestCursor(void)
{
    TactCursor cursor;
    MapGeom    map;

    printf("MapCursor_SetFlags / MapCursor_IsValid\n");
    map.nDimX = 4;
    map.nDimY = 3;

    memset(&cursor, 0, sizeof(cursor));
    MapCursor_SetFlags(&cursor, 7);
    CheckEqI32(cursor.nFlags, 7, "  flags stored");

    cursor.nX = 0;
    cursor.nY = 0;
    Check(MapCursor_IsValid(&cursor, &map), "  origin valid");

    cursor.nX = 39;
    cursor.nY = 29;
    Check(MapCursor_IsValid(&cursor, &map), "  last cell valid");

    cursor.nX = 40;
    Check(!MapCursor_IsValid(&cursor, &map), "  x == 10*dimX rejected");
    cursor.nX = 0;
    cursor.nY = 30;
    Check(!MapCursor_IsValid(&cursor, &map), "  y == 10*dimY rejected");

    cursor.nX = -1;
    Check(!MapCursor_IsValid(&cursor, &map), "  negative x rejected");
    cursor.nX = 0;
    cursor.nY = -1;
    Check(!MapCursor_IsValid(&cursor, &map), "  negative y rejected");

    /* The cursor carries no layer, so unlike Map_IsValidCell there is nothing
     * layer-related to reject. Stated so the asymmetry is deliberate. */
    Check(true, "  no layer argument, matching the original");
}

static void TestSpeed(void)
{
    GameClock  clock;
    GameTimers timers;

    printf("GameSpeed_Set\n");
    timers.pfnKillAll  = Fake_KillAll;
    timers.pfnStartAll = Fake_StartAll;
    timers.pUser       = NULL;
    GameSpeed_SetTimers(&timers);

    memset(&clock, 0, sizeof(clock));

    for (u32 nSpeed = PUFO_SPEED_MIN; nSpeed <= PUFO_SPEED_MAX; ++nSpeed) {
        g_nKill = 0;
        g_nStart = 0;
        Check(GameSpeed_Set(&clock, nSpeed), "  speed in range restarts timers");
        CheckEqI32(clock.nSpeed, (i32)nSpeed, "  speed stored");
        CheckEqI32(g_nKill, 1, "  timers killed once");
        CheckEqI32(g_nStart, 1, "  timers started once");
    }

    /* Above the range the speed is still stored but the timers are left alone,
     * which is the original's behaviour (it only checks `<= 4`). */
    printf("GameSpeed_Set above the supported range\n");
    g_nKill = 0;
    g_nStart = 0;
    clock.nSpeed = 0;
    Check(!GameSpeed_Set(&clock, 5), "  speed 5 does not restart timers");
    CheckEqI32(clock.nSpeed, 5, "  speed 5 still stored");
    CheckEqI32(g_nKill, 0, "  timers untouched");
    CheckEqI32(g_nStart, 0, "  timers untouched");

    /* A backend-less call must not fault. */
    printf("GameSpeed_Set with no timer backend\n");
    GameSpeed_SetTimers(NULL);
    Check(GameSpeed_Set(&clock, 2), "  still reports success");
    CheckEqI32(clock.nSpeed, 2, "  speed stored");
}

static void TestPause(void)
{
    GameClock clock;

    printf("GameTime_Pause\n");
    memset(&clock, 0, sizeof(clock));
    Check(!clock.bPaused, "  not paused initially");
    GameTime_Pause(&clock);
    Check(clock.bPaused, "  paused after the call");
}

/* MapView_SetCamOffset @ 0x276f0.
 *
 *     if (bAdjust) h = 24*layer + b - 20;  else h = b;
 *     v4 = a + 2*(h + 8);
 *     v5 = a/2 - h;
 *     bias = (v5 <= 0) ? -8 : 8;      <- exactly 0 takes the NEGATIVE bias
 *     out0 = v4 / 32;  out1 = (bias + v5) / -16;
 */
static void TestCamOffset(void)
{
    MapGeom   map;
    CamOffset out;

    printf("MapView_SetCamOffset\n");
    map.nDimX = 16;
    map.nDimY = 16;
    memset(&out, 0, sizeof(out));

    /* Without the layer adjustment: h = b. */
    {
        i32 a = 96;
        i32 b = 32;
        i32 h = b;
        i32 v4 = a + 2 * (h + 8);
        i32 v5 = a / 2 - h;
        i32 nBias = (v5 <= 0) ? -8 : 8;

        MapView_SetCamOffset(&map, a, b, false, 0, &out);
        CheckEqI32(out.bScratch40, (u8)(v4 / 32), "  no-adjust: first byte = v4/32");
        CheckEqI32(out.bScratch41, (u8)((nBias + v5) / -16),
                   "  no-adjust: second byte");
    }

    /* With the adjustment the layer shifts the height by 24*layer - 20. */
    {
        i32 a = 96;
        i32 b = 32;
        i32 nLayer = 3;
        i32 h = 24 * nLayer + b - 20;
        i32 v4 = a + 2 * (h + 8);
        i32 v5 = a / 2 - h;
        i32 nBias = (v5 <= 0) ? -8 : 8;

        MapView_SetCamOffset(&map, a, b, true, (u8)nLayer, &out);
        CheckEqI32(out.bScratch40, (u8)(v4 / 32), "  adjust: layer shifts the height");
        CheckEqI32(out.bScratch41, (u8)((nBias + v5) / -16),
                   "  adjust: second byte follows the shifted height");
    }

    /* The bias boundary: v5 == 0 must take the NEGATIVE bias, so pick a,b with
     * a/2 == b. */
    {
        i32 a = 64;
        i32 b = 32;  /* a/2 - b == 0 */
        i32 v5 = a / 2 - b;
        i32 v4 = a + 2 * (b + 8);

        CheckEqI32(v5, 0, "  the constructed boundary really is v5 == 0");
        MapView_SetCamOffset(&map, a, b, false, 0, &out);
        CheckEqI32(out.bScratch41, (u8)((-8 + v5) / -16),
                   "  v5 == 0 takes the negative bias (-8), not +8");
        CheckEqI32(out.bScratch40, (u8)(v4 / 32), "  first byte at the boundary");
    }

    /* A NULL output must not crash. */
    MapView_SetCamOffset(&map, 1, 2, false, 0, NULL);
    Check(true, "  NULL output is safe");
}

/* Map_InitFacingScratch @ 0x26f14. The facing-CHANGE guard is the point: a repeat
 * call with the same facing must do nothing, including the tile-size reset. */
static void TestFacingScratch(void)
{
    MapFacingScratch scratch;
    MapTileSize     size;

    printf("Map_InitFacingScratch\n");
    memset(&scratch, 0, sizeof(scratch));
    memset(&size, 0, sizeof(size));

    /* Tactical: a new facing resets the tile size and both scratch bytes. */
    Check(Map_InitFacingScratch(&scratch, &size, 3, true, 4),
          "  a new facing updates the scratch");
    CheckEqI32(scratch.bStoredFacing, 3, "  the facing was stored");
    CheckEqI32(size.nTileW, 240, "  tile width reset to 240");
    CheckEqI32(size.nTileH, 320, "  tile height reset to 320");
    CheckEqI32(scratch.bScratch34, 9, "  gap_52[0] = 9");
    CheckEqI32(scratch.bScratch35, 24 * 4 / 16 + 47, "  gap_52[1] = 24*layer/16 + 47");

    /* The SAME facing again is a no-op, so a poisoned tile size survives. */
    size.nTileW = 1;
    size.nTileH = 2;
    scratch.bScratch35 = 0xEE;
    Check(!Map_InitFacingScratch(&scratch, &size, 3, true, 4),
          "  the same facing is a no-op");
    CheckEqI32(size.nTileW, 1, "  tile size untouched on the no-op");
    CheckEqI32(scratch.bScratch35, 0xEE, "  scratch byte untouched on the no-op");

    /* A DIFFERENT facing updates again. */
    Check(Map_InitFacingScratch(&scratch, &size, 5, true, 0),
          "  a different facing updates");
    CheckEqI32(scratch.bScratch35, 24 * 0 / 16 + 47, "  layer 0 gives 47");

    /* Non-tactical: the facing is recorded but the tile size is NOT reset. */
    memset(&scratch, 0, sizeof(scratch));
    size.nTileW = 77;
    size.nTileH = 88;
    Check(Map_InitFacingScratch(&scratch, &size, 2, false, 4),
          "  non-tactical still stores the facing");
    CheckEqI32(scratch.bStoredFacing, 2, "  facing stored");
    CheckEqI32(size.nTileW, 77, "  tile size NOT reset outside tactical");
    CheckEqI32(size.nTileH, 88, "  tile height NOT reset either");
    CheckEqI32(scratch.bScratch34, 0, "  scratch byte not written");

    /* The 24*x/16 order matters: it is a truncating multiply-then-divide. */
    memset(&scratch, 0, sizeof(scratch));
    (void)Map_InitFacingScratch(&scratch, &size, 1, true, 3);
    CheckEqI32(scratch.bScratch35, 24 * 3 / 16 + 47,
               "  the multiply-then-divide order is preserved");

    Check(!Map_InitFacingScratch(NULL, &size, 0, true, 0), "  NULL scratch -> false");
}

/* Tact_ScanVictoryTiles @ 0x33264. Both gates must hold, and the 2x2 probe stops at
 * the first cell with no items. */
typedef struct VictoryProbe {
    i32  nCalls;
    i32  anX[4];
    i32  anY[4];
    i32  nEmptyAtCall;  /* 1-based call index that reports empty, or 0 for none */
} VictoryProbe;

static bool Victory_CellHasNoItems(i32 nX, i32 nY, void *pUser)
{
    VictoryProbe *p = (VictoryProbe *)pUser;

    if (p->nCalls < 4) {
        p->anX[p->nCalls] = nX;
        p->anY[p->nCalls] = nY;
    }
    p->nCalls += 1;
    return p->nEmptyAtCall != 0 && p->nCalls == p->nEmptyAtCall;
}

static void TestVictoryTiles(void)
{
    VictoryProbe p;

    printf("Tact_ScanVictoryTiles\n");

    /* Gate 1: no craft origin. */
    memset(&p, 0, sizeof(p));
    Check(!Tact_ScanVictoryTiles(false, PUFO_MISSION_TYPE_ALIEN_BASE, 5, 6,
                                 Victory_CellHasNoItems, &p),
          "  no craft tile -> no victory");
    CheckEqI32(p.nCalls, 0, "  and no probe ran");

    /* Gate 2: the wrong mission type. */
    memset(&p, 0, sizeof(p));
    Check(!Tact_ScanVictoryTiles(true, (u8)(PUFO_MISSION_TYPE_ALIEN_BASE + 1), 5, 6,
                                 Victory_CellHasNoItems, &p),
          "  a non-alien-base mission -> no victory");
    CheckEqI32(p.nCalls, 0, "  and no probe ran");

    /* No cell empty: all four probes run and the result is false. */
    memset(&p, 0, sizeof(p));
    Check(!Tact_ScanVictoryTiles(true, PUFO_MISSION_TYPE_ALIEN_BASE, 5, 6,
                                 Victory_CellHasNoItems, &p),
          "  all four cells occupied -> no victory");
    CheckEqI32(p.nCalls, 4, "  all four probes ran");
    /* The loop order: (0,0), (1,0), then (0,1), (1,1). */
    CheckEqI32(p.anX[0], 5, "  probe 0 x");
    CheckEqI32(p.anY[0], 6, "  probe 0 y");
    CheckEqI32(p.anX[1], 6, "  probe 1 is the next COLUMN");
    CheckEqI32(p.anY[1], 6, "  same row");
    CheckEqI32(p.anX[2], 5, "  probe 2 is the next ROW");
    CheckEqI32(p.anY[2], 7, "  next row");
    CheckEqI32(p.anX[3], 6, "  probe 3");
    CheckEqI32(p.anY[3], 7, "  probe 3");

    /* The first empty cell short-circuits. */
    memset(&p, 0, sizeof(p));
    p.nEmptyAtCall = 1;
    Check(Tact_ScanVictoryTiles(true, PUFO_MISSION_TYPE_ALIEN_BASE, 5, 6,
                                Victory_CellHasNoItems, &p),
          "  an empty first cell -> victory");
    CheckEqI32(p.nCalls, 1, "  and the scan stopped immediately");

    memset(&p, 0, sizeof(p));
    p.nEmptyAtCall = 3;
    Check(Tact_ScanVictoryTiles(true, PUFO_MISSION_TYPE_ALIEN_BASE, 5, 6,
                                Victory_CellHasNoItems, &p),
          "  an empty third cell -> victory");
    CheckEqI32(p.nCalls, 3, "  and it stopped at the third probe");

    Check(!Tact_ScanVictoryTiles(true, PUFO_MISSION_TYPE_ALIEN_BASE, 5, 6, NULL, NULL),
          "  NULL probe -> false");
}

/* Helpers for the MapCursor_SetPos / MapView_EnterLookMode tests. */
static MapCell s_aCursorGrid[4][30][40];

static MapCell *LookupCell(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (nX < 0 || nX >= 40 || nY < 0 || nY >= 30 || nLayer < 0 || nLayer >= 4) {
        return NULL;
    }
    return &s_aCursorGrid[nLayer][nY][nX];
}

static bool FlagBit3(const MapCell *pCell)
{
    return (pCell->bFlags57[0] & 0x08u) != 0;
}

static void RecordSample(u32 nSample, void *pUser)
{
    ((i32 *)pUser)[0] = (i32)nSample;
}

static void TestMapCursorSetPos(void)
{
    /* A small 4x3 grid of cells across 4 layers, with a cell lookup. */
    
    TactCursor cursor;
    MapCursorPos pos;

    printf("MapCursor_SetPos\n");
    memset(s_aCursorGrid, 0, sizeof(s_aCursorGrid));
    memset(&cursor, 0, sizeof(cursor));
    cursor.nX = 1;
    cursor.nY = 1;
    pos.pCursor = &cursor;
    pos.geom.nDimX = 4;
    pos.geom.nDimY = 3;
    pos.bBusy = false;
    pos.bSelUnitLarge = false;
    pos.bSuppressLarge = false;

    MapCursor_SetPos(&pos, 5, 6, LookupCell, s_aCursorGrid);
    CheckEqI32(cursor.nX, 5, "  cursor X stored");
    CheckEqI32(cursor.nY, 6, "  cursor Y stored");
    Check(FlagBit3(&s_aCursorGrid[0][6][5]), "  bit set at the new cell layer 0");
    Check(FlagBit3(&s_aCursorGrid[3][6][5]), "  bit set at the new cell layer 3");
    Check(!FlagBit3(&s_aCursorGrid[0][1][1]), "  old cell cleared");

    /* A large unit marks its 2x2 footprint. */
    memset(s_aCursorGrid, 0, sizeof(s_aCursorGrid));
    pos.bSelUnitLarge = true;
    MapCursor_SetPos(&pos, 2, 2, LookupCell, s_aCursorGrid);
    Check(FlagBit3(&s_aCursorGrid[0][2][2]), "  large: origin set");
    Check(FlagBit3(&s_aCursorGrid[0][2][3]), "  large: right set");
    Check(FlagBit3(&s_aCursorGrid[0][3][3]), "  large: diagonal set");
    Check(FlagBit3(&s_aCursorGrid[0][3][2]), "  large: down set");

    /* SuppressLarge keeps the footprint clear on the set phase. */
    memset(s_aCursorGrid, 0, sizeof(s_aCursorGrid));
    pos.bSuppressLarge = true;
    MapCursor_SetPos(&pos, 2, 2, LookupCell, s_aCursorGrid);
    Check(FlagBit3(&s_aCursorGrid[0][2][2]), "  suppress: origin still set");
    Check(!FlagBit3(&s_aCursorGrid[0][2][3]), "  suppress: right not set");

    /* Out-of-range target clears the old cell but stores nothing. */
    memset(s_aCursorGrid, 0, sizeof(s_aCursorGrid));
    cursor.nX = 1;
    cursor.nY = 1;
    MapCell_SetBFlags57_3(&s_aCursorGrid[0][1][1], true);
    pos.bSelUnitLarge = false;
    pos.bSuppressLarge = false;
    MapCursor_SetPos(&pos, -1, 5, LookupCell, s_aCursorGrid);
    CheckEqI32(cursor.nX, 1, "  out-of-range keeps cursor X");
    Check(!FlagBit3(&s_aCursorGrid[0][1][1]), "  out-of-range still clears old cell");
}

static void TestEnterLookMode(void)
{
    LookModeCtx ctx;
    i32 nSamples[4];
    i32 nCount = 0;

    printf("MapView_EnterLookMode\n");
    memset(&ctx, 0, sizeof(ctx));
    ctx.nMapTileW = 240;
    ctx.nMapTileH = 320;
    ctx.nDimX = 4;
    ctx.nDimY = 3;
    ctx.nCamScrollX = 2;
    ctx.nCamScrollY = 3;
    ctx.nTrajFront4 = 100;
    ctx.nTrajFront5 = 200;
    MapView_EnterLookMode(&ctx, RecordSample, nSamples);
    (void)nCount;
    CheckEqI32(ctx.nUiScreen, 2, "  screen set to 2");
    CheckEqI32(nSamples[0], 16, "  sample 16 played first");
    CheckEqI32(ctx.nCamWorldOff0,
               Map_IsoOffsetA(4, 2, 3) + 100 - 30, "  world offset 0");
    CheckEqI32(ctx.nCamWorldOff1,
               Map_IsoOffsetB(3, 2, 3) + 200 - 40, "  world offset 1");
}

static void TestFocusOnCell(void)
{
    FocusCtx ctx;
    static MapCell cell;
    TactCursor cursor;
    MapGeom geom;
    CamOffset want;
    i32 nA;
    i32 nB;

    printf("MapView_FocusOnCell\n");
    memset(&ctx, 0, sizeof(ctx));
    memset(&cell, 0, sizeof(cell));
    MapCell_Init(&cell, 7, 5, 2);
    cursor.nX = 0;
    cursor.nY = 0;
    cursor.nFlags = 0;

    ctx.nMapTileW = 240;
    ctx.nMapTileH = 320;
    ctx.nDimX = 4;
    ctx.nDimY = 3;
    ctx.nCamScrollX = 999;
    ctx.nCamScrollY = 999;

    /* The scratch pair is whatever SetCamOffset writes for half the tile size with
     * no layer adjustment; the focus then subtracts it from the cell coordinate.
     * Both bytes are read back SIGNED (LDRSB at 0x2C788/0x2C7A0), which the (i8)
     * cast below reproduces. With these tile sizes both come out positive, so the
     * sign only shows in the cast, not in the numbers: SetCamOffset(120, 160) gives
     * (120 + 2*(160+8))/32 = 14 and (120/2 - 160 - 8)/-16 = 6. */
    geom.nDimX = 4;
    geom.nDimY = 3;
    MapView_SetCamOffset(&geom, 120, 160, false, 0, &want);
    nA = 7 - (i32)(i8)want.bScratch40;
    nB = 5 - (i32)(i8)want.bScratch41;

    MapView_FocusOnCell(&ctx, &cell, &cursor);
    CheckEqI32(ctx.camOffset.bScratch40, want.bScratch40, "  scratch 0x4A40");
    CheckEqI32(ctx.camOffset.bScratch41, want.bScratch41, "  scratch 0x4A41");
    CheckEqI32(ctx.nCamScrollX, Map_CenterOffsetX(4, nA, nB), "  camera scroll X");
    CheckEqI32(ctx.nCamScrollY, Map_CenterOffsetY(nA, nB), "  camera scroll Y");
    CheckEqI32(cursor.nFlags, 3, "  cursor layer is the cell layer plus one");

    memset(&ctx, 0, sizeof(ctx));
    ctx.nCamScrollX = 111;
    ctx.nCamScrollY = 222;
    cursor.nFlags = 4;
    MapView_FocusOnCell(&ctx, NULL, &cursor);
    CheckEqI32(ctx.nCamScrollX, 111, "  no cell leaves the scroll alone");
    CheckEqI32(ctx.nCamScrollY, 222, "  no cell leaves the scroll Y alone");
    CheckEqI32(cursor.nFlags, 4, "  no cell leaves the cursor layer alone");

    /* MapView_FocusSelected @ 0x16710 is the same focus plus a sample. The gate byte only
     * chooses whether sample 17 plays, so the camera result must be identical either way.
     * Sound is not asserted here: Sound_Play needs the audio-ready flag, which test_audio
     * covers with its own fake backend. */
    MapView_SetCamOffset(&geom, 120, 160, false, 0, &want);
    nA = 7 - (i32)(i8)want.bScratch40;
    nB = 5 - (i32)(i8)want.bScratch41;

    memset(&ctx, 0, sizeof(ctx));
    ctx.nMapTileW = 240;
    ctx.nMapTileH = 320;
    ctx.nDimX = 4;
    ctx.nDimY = 3;
    cursor.nFlags = 0;
    MapView_FocusSelected(&ctx, &cell, 0, &cursor);
    CheckEqI32(ctx.nCamScrollX, Map_CenterOffsetX(4, nA, nB), "  gate clear still focuses X");
    CheckEqI32(cursor.nFlags, 3, "  gate clear still sets the cursor layer");

    memset(&ctx, 0, sizeof(ctx));
    ctx.nMapTileW = 240;
    ctx.nMapTileH = 320;
    ctx.nDimX = 4;
    ctx.nDimY = 3;
    cursor.nFlags = 0;
    MapView_FocusSelected(&ctx, &cell, 1, &cursor);
    CheckEqI32(ctx.nCamScrollX, Map_CenterOffsetX(4, nA, nB), "  gate set focuses the same X");
    CheckEqI32(ctx.nCamScrollY, Map_CenterOffsetY(nA, nB), "  gate set focuses the same Y");

    memset(&ctx, 0, sizeof(ctx));
    ctx.nCamScrollX = 111;
    ctx.nCamScrollY = 222;
    MapView_FocusSelected(&ctx, NULL, 1, NULL);
    CheckEqI32(ctx.nCamScrollX, 111, "  no cell leaves the focus alone");
    CheckEqI32(ctx.nCamScrollY, 222, "  and no cursor is safe to pass");
}

static void TestCameraBounds(void)
{
    CameraBoundsCtx ctx;
    i32 nX0, nY0, nX1, nY1;

    printf("Tact_GetCameraBounds\n");
    memset(&ctx, 0, sizeof(ctx));
    ctx.nMapDimX = 10;
    ctx.nMapDimY = 8;

    /* No craft: the whole map. */
    Tact_GetCameraBounds(&ctx, &nX0, &nY0, &nX1, &nY1);
    CheckEqI32(nX0, 0, "  whole map X0");
    CheckEqI32(nY0, 0, "  whole map Y0");
    CheckEqI32(nX1, 100, "  whole map X1 = 10*dimX");
    CheckEqI32(nY1, 80, "  whole map Y1 = 10*dimY");

    /* Alien base: the bottom three-tile strip. */
    ctx.bUseCraft = true;
    ctx.nMissionType = 4;
    Tact_GetCameraBounds(&ctx, &nX0, &nY0, &nX1, &nY1);
    CheckEqI32(nX0, 70, "  alien base X0 = 10*(dimX-3)");
    CheckEqI32(nX1, 100, "  alien base X1 = X0+30");
    CheckEqI32(nY0, 70, "  alien base Y0 = 10*dimY-10");
    CheckEqI32(nY1, 80, "  alien base Y1 = 10*dimY");

    /* Craft-centred: type 1 widens X by 20 and Y by 10. */
    ctx.nMissionType = 0;
    ctx.nCraftMapX = 3;
    ctx.nCraftMapY = 4;
    ctx.bCraftType = 1;
    Tact_GetCameraBounds(&ctx, &nX0, &nY0, &nX1, &nY1);
    CheckEqI32(nX0, 30, "  craft X0 = 10*nCraftMapX");
    CheckEqI32(nX1, 50, "  type 1 X1 = X0+20");
    CheckEqI32(nY0, 40, "  craft Y0 = 10*nCraftMapY");
    CheckEqI32(nY1, 50, "  type 1 Y1 = Y0+10");

    /* Types 0 and 2 widen X by 10 and Y by 20. */
    ctx.bCraftType = 0;
    Tact_GetCameraBounds(&ctx, &nX0, &nY0, &nX1, &nY1);
    CheckEqI32(nX1, 40, "  type 0 X1 = X0+10");
    CheckEqI32(nY1, 60, "  type 0 Y1 = Y0+20");
    ctx.bCraftType = 2;
    Tact_GetCameraBounds(&ctx, &nX0, &nY0, &nX1, &nY1);
    CheckEqI32(nX1, 40, "  type 2 X1 = X0+10");
    CheckEqI32(nY1, 60, "  type 2 Y1 = Y0+20");

    /* An unknown type leaves the box at the craft tile. */
    ctx.bCraftType = 9;
    Tact_GetCameraBounds(&ctx, &nX0, &nY0, &nX1, &nY1);
    CheckEqI32(nX1, 30, "  unknown type X1 unchanged");
    CheckEqI32(nY1, 40, "  unknown type Y1 unchanged");
}

/* --- the deploy confirm dialog ------------------------------------------- */

static MapCell  g_aDepCell[8];
static Soldier  g_aDepUnit[8];
static Soldier *g_apDepUnit[8];
static Soldier *g_apDepCiv[2];
static i32      g_nDepSample;
static i32      g_nDepModalX, g_nDepModalY, g_nDepModalN;
static char     g_szMsg1[128], g_szMsg2[128];

static void Dep_Play(i32 nSample, void *pUser)
{
    (void)pUser;
    g_nDepSample = nSample;
}

static void Dep_Modal(i32 nX, i32 nY, i32 nCount, const char *szYes, const char *szNo,
                      void *pUser)
{
    (void)szYes; (void)szNo; (void)pUser;
    g_nDepModalX = nX;
    g_nDepModalY = nY;
    g_nDepModalN = nCount;
}

static const char *Dep_Text(i32 nRecord, void *pUser)
{
    (void)pUser;
    if (nRecord == PUFO_TXTREC_DEPLOY_IN)   { return " in"; }
    if (nRecord == PUFO_TXTREC_DEPLOY_JOIN) { return " out"; }
    return "";
}

static const DeployConfirmHooks g_DepHooks = {Dep_Play, Dep_Modal, Dep_Text, NULL};

static void TestDeployUnits(void)
{
    DeployCountCtx c;
    DeployCounts r;
    TactUiState ui;

    printf("Map_BuildDeployConfirmYesNo -- the deploy buckets\n");

    /* A 10x10-tile map, craft tile (2,2), so the craft rect starts at x 20, y 20 and
     * craft type 1 widens it to x 40, y 30. */
    memset(&c, 0, sizeof(c));
    c.Bounds.bUseCraft = true;
    c.Bounds.nMissionType = 0;
    c.Bounds.nMapDimX = 10;
    c.Bounds.nMapDimY = 10;
    c.Bounds.nCraftMapX = 2;
    c.Bounds.nCraftMapY = 2;
    c.Bounds.bCraftType = 1;
    for (i32 i = 0; i < 8; ++i) {
        g_aDepCell[i].nX = 0;
        g_aDepCell[i].nY = 0;
        g_aDepUnit[i].pCell = &g_aDepCell[i];
        g_aDepUnit[i].uHp = 1;
    }
    /* 0: inside, small. 1: outside, small. 2: inside, big (type 5). 3: outside, big.
     * 4: type 9, never counted. 5: on the far edge, inside. */
    g_aDepCell[0].nX = 25; g_aDepCell[0].nY = 25; g_aDepUnit[0].nUnitType = 1;
    g_aDepCell[1].nX = 51; g_aDepCell[1].nY = 25; g_aDepUnit[1].nUnitType = 2;
    g_aDepCell[2].nX = 30; g_aDepCell[2].nY = 30; g_aDepUnit[2].nUnitType = 5;
    g_aDepCell[3].nX = 19; g_aDepCell[3].nY = 20; g_aDepUnit[3].nUnitType = 8;
    g_aDepCell[4].nX = 25; g_aDepCell[4].nY = 25; g_aDepUnit[4].nUnitType = 9;
    g_aDepCell[5].nX = 40; g_aDepCell[5].nY = 30; g_aDepUnit[5].nUnitType = 3;
    c.apXcom = g_apDepUnit;
    c.nXcomCount = 6;
    c.apCiv = g_apDepCiv;
    c.nCivCount = 2;
    for (i32 i = 0; i < 6; ++i) {
        g_apDepUnit[i] = &g_aDepUnit[i];
    }
    g_apDepCiv[0] = &g_aDepUnit[6];
    g_apDepCiv[1] = &g_aDepUnit[7];
    g_aDepCell[6].nX = 25; g_aDepCell[6].nY = 25; g_aDepUnit[6].nUnitType = 2;
    g_aDepCell[7].nX = 25; g_aDepCell[7].nY = 25; g_aDepUnit[7].nUnitType = 2;
    g_aDepUnit[7].uHp = 0;   /* 0x18C24: no hit points, not counted */

    Tact_CountDeployUnits(&c, &r);
    CheckEqI32(r.nSmallIn, 3, "  three small units inside (inclusive edges)");
    CheckEqI32(r.nSmallOut, 1, "  one small unit outside");
    CheckEqI32(r.nBigIn, 1, "  one 4..8 unit inside");
    CheckEqI32(r.nBigOut, 1, "  one 4..8 unit outside");

    /* The alien-base mission type replaces the craft rect with the bottom
     * three-tile strip: x 70..100, y 90..100. */
    c.Bounds.nMissionType = PUFO_MISSION_TYPE_ALIEN_BASE;
    g_aDepCell[0].nX = 75; g_aDepCell[0].nY = 95;
    Tact_CountDeployUnits(&c, &r);
    CheckEqI32(r.nSmallIn, 1, "  only unit 0 is in the alien-base strip");
    CheckEqI32(r.nSmallOut, 3, "  the other small units and the civilian are outside");
    CheckEqI32(r.nBigIn, 0, "  the type-5 unit at 30,30 is now outside");
    CheckEqI32(r.nBigOut, 2, "  both 4..8 units are outside");

    /* The dialog half. */
    c.Bounds.nMissionType = 0;
    g_aDepCell[0].nX = 25; g_aDepCell[0].nY = 25;
    memset(&ui, 0, sizeof(ui));
    ui.nUiScreen = 0;
    g_nDepSample = 0;
    g_nDepModalN = 0;
    g_szMsg1[0] = 0;
    g_szMsg2[0] = 0;
    {
        DeployConfirmCtx dc;
        memset(&dc, 0, sizeof(dc));
        dc.Count = c;
        dc.pUi = &ui;
        dc.aszMsg1 = g_szMsg1;
        dc.aszMsg2 = g_szMsg2;
        dc.nMsgCap = 128;
        dc.pHooks = &g_DepHooks;
        CheckEqI32(Map_BuildDeployConfirmYesNo(&dc), 9, "  the handler returns 9");
    }
    CheckEqI32(g_nDepSample, 16, "  sample 16 plays before the modal");
    CheckEqI32(g_nDepModalX, 170, "  the modal is at 170,1069");
    CheckEqI32(g_nDepModalY, 1069, "  with y 1069");
    CheckEqI32(g_nDepModalN, 5, "  and five buttons");
    CheckEqI32((i32)ui.nUiScreen, 9, "  nUiScreen becomes 9");
    Check(strcmp(g_szMsg1, "3 in1 out") == 0, "  message 1 is in-count, text 28, big-in, text 29");
    Check(strcmp(g_szMsg2, "1 in1 out") == 0, "  message 2 uses the outside counts");
}

/* A lookup that records every coordinate MapCursor_SetPos asks for, so the park at (-2,-2) is
 * observable. Out-of-range requests get a scratch cell instead of walking off the grid. */
static i32     s_aSelReqX[64];
static i32     s_aSelReqY[64];
static i32     s_nSelReq;
static MapCell s_selOob;

static MapCell *SelLookup(i32 nX, i32 nY, i32 nLayer, void *pUser)
{
    (void)pUser;
    if (s_nSelReq < 64) {
        s_aSelReqX[s_nSelReq] = nX;
        s_aSelReqY[s_nSelReq] = nY;
    }
    s_nSelReq += 1;
    if (nX < 0 || nX >= 40 || nY < 0 || nY >= 30 || nLayer < 0 || nLayer >= 4) {
        return &s_selOob;
    }
    return &s_aCursorGrid[nLayer][nY][nX];
}

static bool Sel_NotBusy(void *pUser)    { (void)pUser; return false; }
static bool Sel_NoRepaint(void *pUser)  { (void)pUser; return false; }
static void Sel_NoRender(void *pUser)   { (void)pUser; }

static char s_aSelRankLow[9][PUFO_RANK_NAME_STRIDE];

static void TestSelectUnit(void)
{
    TactCursor    cursor;
    MapCursorPos  pos;
    StatusStack   stack;
    StatusHooks   hooks = { Sel_NotBusy, Sel_NoRepaint, Sel_NoRender, NULL };
    TactSelectCtx ctx;
    Soldier       unitA, unitB;
    Soldier      *pPrev = NULL;

    printf("Tact_SelectUnit @ 0x2c6c4\n");

    MapCell_Init(&s_selOob, 0, 0, 0);
    for (i32 i = 0; i < 9; ++i) {
        pufo_strcpy(s_aSelRankLow[i], "Squad");
    }

    memset(s_aCursorGrid, 0, sizeof(s_aCursorGrid));
    memset(&cursor, 0, sizeof(cursor));
    cursor.nX = 1;
    cursor.nY = 1;
    pos.pCursor = &cursor;
    pos.geom.nDimX = 4;
    pos.geom.nDimY = 3;
    pos.bBusy = false;
    pos.bSelUnitLarge = false;
    pos.bSuppressLarge = false;

    memset(&stack, 0, sizeof(stack));

    memset(&unitA, 0, sizeof(unitA));
    unitA.nUnitType = 0;
    unitA.bStatus = 0;
    pufo_strcpy(unitA.szName, "Hallow");
    unitA.pCell = &s_aCursorGrid[0][1][2];
    unitA.pCell->nX = 2;
    unitA.pCell->nY = 1;

    memset(&unitB, 0, sizeof(unitB));
    unitB.nUnitType = 0;
    unitB.bStatus = 0;
    pufo_strcpy(unitB.szName, "Sheeran");
    unitB.pCell = &s_aCursorGrid[0][2][3];
    unitB.pCell->nX = 3;
    unitB.pCell->nY = 2;

    memset(&ctx, 0, sizeof(ctx));
    ctx.ppSelUnit = &pPrev;
    ctx.pCursorPos = &pos;
    ctx.pfnLookup = SelLookup;
    ctx.pStatus = &stack;
    ctx.pStatusHooks = &hooks;
    ctx.panRankLow = &s_aSelRankLow[0][0];
    ctx.panRankOther = &s_aSelRankLow[0][0];

    /* The first selection of a mission: nothing was selected before, so there is no park. The park is
     * only visible through bBusy, because (-2,-2) fails MapCursor_SetPos's bounds check and so it
     * clears the old markers without moving the cursor. */
    pos.bBusy = true;
    Tact_SelectUnit(&ctx, &unitA);
    Check(!FlagBit3(&s_aCursorGrid[0][1][2]),
          "with nothing selected before, there is no park to clear the old cell");
    CheckEqI32(cursor.nX, 1, "and the busy cursor is not moved at all");
    CheckEqI32(cursor.nY, 1, "both coordinates");
    CheckEqI32(stack.nCount, 1, "the name is still pushed");
    Check(strcmp(stack.aszMsg[0], "Squad: Hallow") == 0,
          "pushed in the mode 0 rank-colon-name form");
    Check(pPrev == &unitA, "the selection slot at Map+0x15334 is written");

    /* Move the cursor for real, then select a different unit with the cursor busy. The park runs and
     * clears the previous cell while the move itself is suppressed. */
    pos.bBusy = false;
    Tact_SelectUnit(&ctx, &unitA);
    Check(FlagBit3(&s_aCursorGrid[0][1][2]), "the move marks the unit's cell");
    CheckEqI32(cursor.nX, 2, "and parks the cursor there");
    CheckEqI32(stack.nCount, 1,
               "re-selecting the same unit names it again: the compare and the park read one slot");

    pos.bBusy = true;
    Tact_SelectUnit(&ctx, &unitB);
    Check(!FlagBit3(&s_aCursorGrid[0][2][3]),
          "the new cell is not marked, because the busy move is suppressed");
    CheckEqI32(cursor.nX, 2, "the park left the cursor where it was");
    CheckEqI32(cursor.nY, 1, "both coordinates");
    CheckEqI32(stack.nCount, 2, "and the new unit was still named");
    Check(strcmp(stack.aszMsg[1], "Squad: Sheeran") == 0, "with its own name");
    Check(pPrev == &unitB, "the slot follows the last call");

    /* Re-selecting the stored unit pushes nothing but still parks and moves. */
    pos.bBusy = false;
    Tact_SelectUnit(&ctx, &unitB);
    CheckEqI32(stack.nCount, 2, "re-selecting the stored unit pushes nothing");
    CheckEqI32(cursor.nX, 3, "but it still moves the cursor");
    CheckEqI32(cursor.nY, 2, "both coordinates");
    Check(FlagBit3(&s_aCursorGrid[0][2][3]), "and marks the new cell");

    /* An inoperative unit moves the cursor but is never named. */
    unitA.bStatus = 3;
    Tact_SelectUnit(&ctx, &unitA);
    CheckEqI32(stack.nCount, 2, "an inoperative unit pushes nothing");
    CheckEqI32(cursor.nX, 2, "and its cell is still selected");
    CheckEqI32(cursor.nY, 1, "both coordinates");
    unitA.bStatus = 0;
}

static Ship    s_aTeamShip[2];
static Ship   *s_apTeamShip[2];
static Soldier s_aTeamUnit[4];
static Soldier *s_apTeamUnit[4];
static Soldier *s_aTeamXcom[30];
static u8      s_aTeamDeploy[104 * 3];
static u16     s_aTeamStat[10];
static u8      g_nTeamCount;

/* Four soldiers, ordered so that each mode stops at a different place.
 *
 *   0: base 1, aboard craft 0
 *   1: base 1, aboard nothing (255)
 *   2: base 1, aboard craft 1
 *   3: base 255, aboard nothing
 *
 * nUnk436 and uInvBurden are sentinels: nUnk436 is cleared for every soldier the walk REACHES, and
 * uInvBurden is rewritten to 1 only for soldiers that made it into the roster. Together they separate
 * "visited then rejected" from "never visited". */
static void TeamFixtureReset(void)
{
    for (i32 i = 0; i < 4; ++i) {
        memset(&s_aTeamUnit[i], 0, sizeof(s_aTeamUnit[i]));
        s_aTeamUnit[i].nUnitType  = 0;   /* not large, so placement stays single-cell */
        s_aTeamUnit[i].nUnk436    = 0x7A;
        s_aTeamUnit[i].uInvBurden = 99;
        s_aTeamUnit[i].bStatus    = 5;
        s_aTeamUnit[i].bFacing    = 3;
        s_aTeamUnit[i].nAnimPose  = 7;
        s_apTeamUnit[i] = &s_aTeamUnit[i];
    }
    s_aTeamUnit[0].nKind = 1;   s_aTeamUnit[0].bUnk183 = 0;
    s_aTeamUnit[1].nKind = 1;   s_aTeamUnit[1].bUnk183 = 255;
    s_aTeamUnit[2].nKind = 1;   s_aTeamUnit[2].bUnk183 = 1;
    s_aTeamUnit[3].nKind = 255; s_aTeamUnit[3].bUnk183 = 255;

    memset(s_aTeamXcom, 0, sizeof(s_aTeamXcom));
    memset(s_aTeamDeploy, 0, sizeof(s_aTeamDeploy));
    s_aTeamDeploy[1 * 104 + 0] = 1;
    s_aTeamDeploy[1 * 104 + 1] = 2;
    s_aTeamDeploy[1 * 104 + 2] = 0;
    s_aTeamDeploy[2 * 104 + 0] = 4;
    s_aTeamDeploy[2 * 104 + 1] = 6;
    s_aTeamDeploy[2 * 104 + 2] = 1;

    s_aTeamStat[0] = 45;
    s_aTeamStat[1] = 78;
    s_aTeamStat[9] = 12;

    s_apTeamShip[0] = &s_aTeamShip[0];
    s_apTeamShip[1] = &s_aTeamShip[1];
    memset(s_aTeamShip, 0, sizeof(s_aTeamShip));
    s_aTeamShip[0].bAtBase = 1;
    s_aTeamShip[1].bAtBase = 1;
}

static i32 TeamRunCase(TactTeamSelectEnv *pEnv)
{
    for (i32 i = 0; i < 4; ++i) {
        s_aTeamUnit[i].nUnk436    = 0x7A;
        s_aTeamUnit[i].uInvBurden = 99;
        s_aTeamUnit[i].bStatus    = 5;
        s_aTeamUnit[i].bFacing    = 3;
        s_aTeamUnit[i].nAnimPose  = 7;
        s_aTeamUnit[i].pCell      = NULL;
    }
    memset(s_aTeamXcom, 0, sizeof(s_aTeamXcom));
    g_nTeamCount = 0;
    pEnv->pnXcomCount = &g_nTeamCount;
    return Tact_SelectTeamUnits(pEnv);
}

static void TestSelectTeamUnits(void)
{
    TactCursor    cursor;
    MapCursorPos  pos;
    StatusStack   stack;
    StatusHooks   hooks = { Sel_NotBusy, Sel_NoRepaint, Sel_NoRender, NULL };
    InvStatHooks  inv = { NULL, NULL, NULL, NULL, NULL };
    TactSelectCtx selCtx;
    TactTeamSelectEnv env;
    Soldier      *pSel = NULL;

    printf("Tact_SelectTeamUnits @ 0x39744\n");

    TeamFixtureReset();

    memset(s_aCursorGrid, 0, sizeof(s_aCursorGrid));
    s_aCursorGrid[1][26][14].nX = 14; s_aCursorGrid[1][26][14].nY = 26; s_aCursorGrid[1][26][14].nZ = 1;
    s_aCursorGrid[0][22][11].nX = 11; s_aCursorGrid[0][22][11].nY = 22; s_aCursorGrid[0][22][11].nZ = 0;

    memset(&cursor, 0, sizeof(cursor));
    pos.pCursor = &cursor;
    pos.geom.nDimX = 8;
    pos.geom.nDimY = 8;
    pos.bBusy = false;
    pos.bSelUnitLarge = false;
    pos.bSuppressLarge = false;
    memset(&stack, 0, sizeof(stack));

    memset(&selCtx, 0, sizeof(selCtx));
    selCtx.ppSelUnit = &pSel;
    selCtx.pCursorPos = &pos;
    selCtx.pfnLookup = LookupCell;
    selCtx.pStatus = &stack;
    selCtx.pStatusHooks = &hooks;
    selCtx.panRankLow = &s_aSelRankLow[0][0];
    selCtx.panRankOther = &s_aSelRankLow[0][0];

    memset(&env, 0, sizeof(env));
    env.apSoldiers = s_apTeamUnit;
    env.nSoldierCount = 4;
    env.apCraft = (struct Ship **)s_apTeamShip;
    env.nCraftIndex = 0;
    env.nCraftMapX = 1;   /* 10 cells */
    env.nCraftMapY = 2;   /* 20 cells */
    env.nSelectedBase = 1;
    env.pDeployOffsets = s_aTeamDeploy;
    env.apXcomUnits = s_aTeamXcom;
    env.auStat = s_aTeamStat;
    env.nStatCount = 10;
    env.pfnLookup = LookupCell;
    env.pSelectCtx = &selCtx;
    env.pInvHooks = &inv;

    /* The anchor is 10 * craft tile plus the record pair, and the record is chosen by the layer count.
     * Two different layer counts against the same craft position prove the 104-byte stride. */
    env.bMissionMode = 4;   /* accept everything, so the anchor is the only thing under test */
    env.nMapLayers = 2;
    (void)TeamRunCase(&env);
    CheckEqI32(g_nTeamCount, 4, "  mode above 3 accepts every soldier");
    Check(s_aTeamUnit[0].pCell == &s_aCursorGrid[1][26][14],
          "  layer count 2 gives x 10+4, y 20+6, layer 1");

    env.nMapLayers = 1;
    (void)TeamRunCase(&env);
    Check(s_aTeamUnit[0].pCell == &s_aCursorGrid[0][22][11],
          "  layer count 1 gives x 10+1, y 20+2, layer 0");

    env.nMapLayers = 0;
    (void)TeamRunCase(&env);
    Check(s_aTeamUnit[0].pCell == &s_aCursorGrid[0][0][0],
          "  a zero layer count skips the table and anchors at (0, 0, 0)");

    /* Mode 1 is the base-deploy rule: nKind must equal the selected base, and the soldier's craft must
     * either be absent (255) or sitting at a base. Soldier 3 has the wrong base, so the walk stops there. */
    env.bMissionMode = 1;
    env.nMapLayers = 2;
    CheckEqI32(TeamRunCase(&env), 3, "  mode 1 takes the three soldiers of the selected base");
    Check(s_aTeamXcom[0] == &s_aTeamUnit[0] && s_aTeamXcom[1] == &s_aTeamUnit[1] &&
          s_aTeamXcom[2] == &s_aTeamUnit[2] && s_aTeamXcom[3] == NULL,
          "  in roster order, with nothing past them");
    CheckEqI32(s_aTeamUnit[3].nUnk436, 0,
               "  the rejected soldier is still visited first: 0x1B4 is cleared before any test");

    /* Craft 1 is not at base, so soldier 2 is reached, cleared, and then rejected. */
    s_aTeamShip[1].bAtBase = 0;
    CheckEqI32(TeamRunCase(&env), 2, "  a soldier aboard a craft that is not at base is rejected");
    CheckEqI32(s_aTeamUnit[2].nUnk436, 0,
               "  he is still visited, so his 0x1B4 field is cleared");
    CheckEqI32(s_aTeamUnit[3].nUnk436, 0x7A, "  and the walk still stops at him");
    s_aTeamShip[1].bAtBase = 1;

    /* Modes 0, 2 and 3 all compare bUnk183 against the launch craft. */
    env.bMissionMode = 0;
    env.nCraftIndex = 0;
    CheckEqI32(TeamRunCase(&env), 1, "  mode 0 takes only soldiers aboard craft 0");
    env.bMissionMode = 3;
    CheckEqI32(TeamRunCase(&env), 1, "  mode 3 uses the same rule");
    env.bMissionMode = 2;
    env.nCraftIndex = 255;
    pSel = NULL;
    CheckEqI32(TeamRunCase(&env), 0, "  mode 2 with craft 255 takes nobody");
    Check(pSel == NULL, "  an empty roster selects no unit");
    CheckEqI32(s_aTeamUnit[1].uInvBurden, 99, "  and recalculates nobody");
    Check(s_aTeamUnit[1].nUnk436 == 0x7A && s_aTeamUnit[2].nUnk436 == 0x7A &&
          s_aTeamUnit[3].nUnk436 == 0x7A,
          "  rejection at the first soldier leaves every later soldier completely untouched");
    env.nCraftIndex = 0;

    /* Anything above 3 accepts without looking at the soldier. */
    env.bMissionMode = 200;
    CheckEqI32(TeamRunCase(&env), 4, "  a mode above 3 accepts unconditionally");

    /* The base byte is loaded with LDRSB. A value of 0xFF sign-extends to -1 and then matches no nKind,
     * which an unsigned compare would not do: soldier 3 has nKind 255 and is still rejected. */
    env.bMissionMode = 1;
    env.nSelectedBase = (i8)0xFF;
    CheckEqI32(TeamRunCase(&env), 0,
               "  the signed base load means 0xFF matches no nKind, even soldier 3 with nKind 255");
    env.nSelectedBase = 1;

    /* What an accepted soldier gets. */
    env.bMissionMode = 1;
    (void)TeamRunCase(&env);
    CheckEqI32(s_aTeamUnit[0].bFacing, 0, "  facing reset to 0");
    CheckEqI32(s_aTeamUnit[0].nAnimPose, 1, "  animation pose reset to 1");
    CheckEqI32(s_aTeamUnit[0].bStatus, 0, "  bStatus cleared");
    CheckEqI32(s_aTeamUnit[0].uTu, 45, "  TU from stat 0");
    CheckEqI32(s_aTeamUnit[0].uHp, 78, "  HP from stat 1");
    CheckEqI32(s_aTeamUnit[0].uStunOrArmor, 12, "  stun or armour from stat 9");
    CheckEqI32(s_aTeamUnit[0].uInvBurden, 1, "  inventory stats recalculated");
    CheckEqI32(s_aTeamUnit[3].uInvBurden, 99, "  the unprocessed soldier is not recalculated");

    /* The first roster entry is selected, which moves the cursor onto the cell it was placed on. */
    Check(pSel == &s_aTeamUnit[0], "  the first roster entry is selected through Tact_SelectUnit");
    CheckEqI32(cursor.nX, 14, "  the cursor follows the placement x");
    CheckEqI32(cursor.nY, 26, "  and the placement y");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestCursor();
    TestSpeed();
    TestPause();
    TestCamOffset();
    TestFacingScratch();
    TestVictoryTiles();
    TestMapCursorSetPos();
    TestEnterLookMode();
    TestFocusOnCell();
    TestCameraBounds();
    TestDeployUnits();
    TestSelectUnit();
    TestSelectTeamUnits();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
