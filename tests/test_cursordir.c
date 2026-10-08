/* test_cursordir.c - the tactical cursor-direction state machine
 * (TactHud_ComputeCursorDirState @ 0x1a7c8, TactHud_TickCursorDir @ 0x1e888). */
#include <stdio.h>
#include <string.h>

#include "game/pufo_cursordir.h"

static int g_nFailures;

static void CheckEqI32(i32 nGot, i32 nWant, const char *pszWhat)
{
    if (nGot != nWant) {
        printf("  FAIL %s: got %d want %d\n", pszWhat, (int)nGot, (int)nWant);
        g_nFailures += 1;
    }
}

static void TestCompute(void)
{
    CursorDirState s;

    memset(&s, 0, sizeof(s));
    s.bSlot8Byte3 = 0; s.bSlot8Byte2 = 0;
    TactHud_ComputeCursorDirState(&s);
    CheckEqI32(s.nSlot9, 0, "(0,0) step 0");
    CheckEqI32(s.nSlot10, 10, "(0,0) countdown 10");

    s.bSlot8Byte3 = 0; s.bSlot8Byte2 = 1;
    TactHud_ComputeCursorDirState(&s);
    CheckEqI32(s.nSlot9, -1, "(0,1) step -1");
    CheckEqI32(s.nSlot10, 9, "(0,1) countdown 9");

    s.bSlot8Byte3 = 1; s.bSlot8Byte2 = 0;
    TactHud_ComputeCursorDirState(&s);
    CheckEqI32(s.nSlot9, 1, "(1,0) step 1");
    CheckEqI32(s.nSlot10, 1, "(1,0) countdown 1");

    s.bSlot8Byte3 = 1; s.bSlot8Byte2 = 1;
    TactHud_ComputeCursorDirState(&s);
    CheckEqI32(s.nSlot9, 0, "(1,1) step 0");
    CheckEqI32(s.nSlot10, 0, "(1,1) countdown 0");
}

static void TestTickAdvances(void)
{
    CursorDirState s;

    /* nSlot10 not 0 or 10 -> advance by nSlot9. byte2 1 with busy means no
     * recompute, so the pure countdown advance is observable. */
    memset(&s, 0, sizeof(s));
    s.bSlot8Byte2 = 1;
    s.nSlot10 = 5;
    s.nSlot9 = -1;
    TactHud_TickCursorDir(&s, true, false, false);
    CheckEqI32(s.nSlot10, 4, "countdown advances by the step");

    /* 0 and 10 are the two parked values. */
    memset(&s, 0, sizeof(s));
    s.bSlot8Byte2 = 1;
    s.nSlot10 = 10;
    s.nSlot9 = 1;
    TactHud_TickCursorDir(&s, true, false, false);
    CheckEqI32(s.nSlot10, 10, "countdown 10 does not advance");

    memset(&s, 0, sizeof(s));
    s.bSlot8Byte2 = 1;
    s.nSlot10 = 0;
    s.nSlot9 = 1;
    TactHud_TickCursorDir(&s, true, false, false);
    CheckEqI32(s.nSlot10, 0, "countdown 0 does not advance");
}

static void TestTickShift(void)
{
    CursorDirState s;

    /* Busy, no track unit, no flag -> new byte2 = 1. Old byte2 0 != 1, so the
     * direction is recomputed for the shifted pair (byte3 0, byte2 1). */
    memset(&s, 0, sizeof(s));
    s.bSlot8Byte2 = 0;
    s.bSlot8Byte3 = 0;
    s.nSlot10 = 5;
    TactHud_TickCursorDir(&s, true, false, false);
    CheckEqI32(s.bSlot8Byte2, 1, "busy sets byte2 to 1");
    CheckEqI32(s.bSlot8Byte3, 0, "old byte2 shifts into byte3");
    CheckEqI32(s.nSlot9, -1, "recompute for (byte3 0, byte2 1)");
    CheckEqI32(s.nSlot10, 9, "recomputed countdown 9");

    /* Not busy, old byte2 0 == the expected value, so NO recompute happens: the
     * shifted pair (0,0) is only reachable through a direct compute call. */
    memset(&s, 0, sizeof(s));
    s.bSlot8Byte2 = 0;
    s.bSlot8Byte3 = 1;
    s.nSlot10 = 4;
    s.nSlot9 = -1;
    TactHud_TickCursorDir(&s, false, false, false);
    CheckEqI32(s.bSlot8Byte2, 0, "not busy sets byte2 to 0");
    CheckEqI32(s.bSlot8Byte3, 0, "old byte2 shifts in");
    CheckEqI32(s.nSlot9, -1, "no recompute leaves the step");
    CheckEqI32(s.nSlot10, 3, "countdown still advanced once");

    /* Old byte2 1, new byte2 1 -> no recompute, countdown advanced once. */
    memset(&s, 0, sizeof(s));
    s.bSlot8Byte2 = 1;
    s.bSlot8Byte3 = 0;
    s.nSlot10 = 4;
    s.nSlot9 = -1;
    TactHud_TickCursorDir(&s, true, false, false);
    CheckEqI32(s.bSlot8Byte2, 1, "still 1");
    CheckEqI32(s.bSlot8Byte3, 1, "old byte2 1 shifts in");
    CheckEqI32(s.nSlot9, -1, "no recompute leaves the step");
    CheckEqI32(s.nSlot10, 3, "countdown still advanced once");

    /* The track-unit and unk70115 flags both force the not-busy path, which
     * recomputes for old byte2 1. */
    memset(&s, 0, sizeof(s));
    s.bSlot8Byte2 = 1;
    TactHud_TickCursorDir(&s, true, true, false);
    CheckEqI32(s.bSlot8Byte2, 0, "track unit forces byte2 0");
    memset(&s, 0, sizeof(s));
    s.bSlot8Byte2 = 1;
    TactHud_TickCursorDir(&s, true, false, true);
    CheckEqI32(s.bSlot8Byte2, 0, "unk70115 forces byte2 0");
}

int main(void)
{
    TestCompute();
    TestTickAdvances();
    TestTickShift();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
