/* test_tactctl.c - checks cursor bounds and game-speed/timer control.
 *
 * The cursor field aliasing is the thing worth pinning: MapCursor_* reach their
 * fields through a char array that IDA names pad86309a, and those offsets resolve
 * onto named Map fields. The test asserts the resolved behaviour, not the alias.
 */
#include "game/pufo_tactctl.h"

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
    TestCameraBounds();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
