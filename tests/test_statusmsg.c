/* test_statusmsg.c - checks the on-screen status message stack.
 *
 * The push policy is the interesting part: below three messages a push always
 * appends, but at three or more a BUSY game drops the message entirely. Both
 * thresholds (3 and 15) come straight from the original's guards.
 */
#include "game/pufo_statusmsg.h"

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

static void CheckEqStr(const char *pszGot, const char *pszWant, const char *pszWhat)
{
    if (strcmp(pszGot, pszWant) == 0) {
        printf("  ok   %s (\"%s\")\n", pszWhat, pszGot);
    } else {
        printf("  FAIL %s: got \"%s\", want \"%s\"\n", pszWhat, pszGot, pszWant);
        g_nFailures += 1;
    }
}

/* --- hooks ---------------------------------------------------------------- */

typedef struct HookState {
    bool bBusy;
    bool bShouldRender;
    i32  nRenders;
    i32  nColors;
    i32  nDraws;
    char aDrawn[16][PUFO_STATUS_REC_BYTES];
    i32  anDrawY[16];
    i32  anDrawX[16];
    bool bInputEnabled;
    bool bOperative;
    i32  nHudPadY;
} HookState;

static HookState g_hs;

static void HsReset(void)
{
    memset(&g_hs, 0, sizeof(g_hs));
}

static bool H_IsBusy(void *pUser)
{
    (void)pUser;
    return g_hs.bBusy;
}

static bool H_ShouldRender(void *pUser)
{
    (void)pUser;
    return g_hs.bShouldRender;
}

static void H_Render(void *pUser)
{
    (void)pUser;
    g_hs.nRenders += 1;
}

static const StatusHooks g_pushHooks = { H_IsBusy, H_ShouldRender, H_Render, NULL };

static void H_SetColor(u8 r1, u8 g1, u8 b1, u8 r2, u8 g2, u8 b2, void *pUser)
{
    (void)r1; (void)g1; (void)b1; (void)r2; (void)g2; (void)b2; (void)pUser;
    g_hs.nColors += 1;
}

static void H_Draw(i32 x, i32 y, const char *pszMsg, void *pUser)
{
    (void)y;
    (void)pUser;
    if (g_hs.nDraws < 16) {
        snprintf(g_hs.aDrawn[g_hs.nDraws], sizeof(g_hs.aDrawn[0]), "%s", pszMsg);
        g_hs.anDrawY[g_hs.nDraws] = y;
        g_hs.anDrawX[g_hs.nDraws] = x;
    }
    g_hs.nDraws += 1;
}

static bool H_InputEnabled(void *pUser)
{
    (void)pUser;
    return g_hs.bInputEnabled;
}

static bool H_SelectedOperative(void *pUser)
{
    (void)pUser;
    return g_hs.bOperative;
}

static i32 H_HudPadY(void *pUser)
{
    (void)pUser;
    return g_hs.nHudPadY;
}

static const StatusDrawHooks g_drawHooks = {
    H_SetColor, H_Draw, H_InputEnabled, H_SelectedOperative, H_IsBusy, H_HudPadY, NULL
};

/* --- tests ---------------------------------------------------------------- */

static void TestPushSimple(void)
{
    StatusStack st;

    printf("StatusMsg_Push -- append below the threshold\n");
    memset(&st, 0, sizeof(st));
    HsReset();

    Check(StatusMsg_Push(&st, "one", false, &g_pushHooks), "  first push stored");
    CheckEqI32(st.nCount, 1, "  count 1");
    CheckEqStr(st.aszMsg[0], "one", "  stored at index 0");
    CheckEqI32(st.anTtl[0], PUFO_STATUS_TTL, "  TTL is 90");

    Check(StatusMsg_Push(&st, "two", false, &g_pushHooks), "  second push stored");
    Check(StatusMsg_Push(&st, "three", false, &g_pushHooks), "  third push stored");
    CheckEqI32(st.nCount, 3, "  count 3 -- the append threshold");
    CheckEqStr(st.aszMsg[0], "one", "  oldest still at index 0");
    CheckEqStr(st.aszMsg[2], "three", "  newest at index 2");

    /* Index 0 is the OLDEST; a full shift drops it. */
    Check(StatusMsg_Push(&st, "four", false, &g_pushHooks), "  fourth push stored");
    CheckEqI32(st.nCount, 3, "  count stays 3 (oldest dropped)");
    CheckEqStr(st.aszMsg[0], "two", "  index 0 is now the old 'two'");
    CheckEqStr(st.aszMsg[2], "four", "  newest at index 2");
}

static void TestBusyDrops(void)
{
    StatusStack st;

    printf("StatusMsg_Push -- a busy game DROPS messages above the threshold\n");
    memset(&st, 0, sizeof(st));
    HsReset();

    /* Busy and not forced: dropped at any count. */
    g_hs.bBusy = true;
    Check(!StatusMsg_Push(&st, "dropped", false, &g_pushHooks),
          "  busy + not forced is dropped");
    CheckEqI32(st.nCount, 0, "  nothing stored");

    /* Forced always goes through. */
    Check(StatusMsg_Push(&st, "forced", true, &g_pushHooks), "  forced is stored");
    CheckEqI32(st.nCount, 1, "  count 1");

    /* Below the threshold, busy still appends. */
    Check(StatusMsg_Push(&st, "b2", true, &g_pushHooks), "  second (forced)");
    CheckEqI32(st.nCount, 2, "  count 2");

    /* At exactly the threshold with busy set and a forced push: it does NOT shift
     * (the `!busy || full` condition is false), so the message is DROPPED. This is
     * the original's asymmetry. */
    Check(StatusMsg_Push(&st, "b3", true, &g_pushHooks), "  third (forced) stores");
    CheckEqI32(st.nCount, 3, "  count 3");
    Check(!StatusMsg_Push(&st, "b4", true, &g_pushHooks),
          "  busy at count 3 DROPS the forced message too");
    CheckEqI32(st.nCount, 3, "  count unchanged");
    CheckEqStr(st.aszMsg[2], "b3", "  newest is still b3");
}

static void TestFullBuffer(void)
{
    StatusStack st;

    printf("StatusMsg_Push -- the stack SATURATES at 3 and never reaches 15\n");
    memset(&st, 0, sizeof(st));
    HsReset();
    /* Not busy: the middle branch shifts the oldest out and appends, so the count
     * returns to exactly 3 every time. It cannot grow. */
    g_hs.bBusy = false;

    for (i32 i = 0; i < 40; ++i) {
        char sz[32];

        snprintf(sz, sizeof(sz), "m%d", i);
        Check(StatusMsg_Push(&st, sz, true, &g_pushHooks), "  pushed");
    }
    CheckEqI32(st.nCount, PUFO_STATUS_APPEND_MAX,
               "  forty pushes still leave exactly 3 records");
    /* The three newest survive; everything older was shifted out. */
    CheckEqStr(st.aszMsg[0], "m37", "  the window slid to the newest three");
    CheckEqStr(st.aszMsg[2], "m39", "  newest is the last push");
    /* The capacity of 15 is therefore UNREACHABLE: the `count >= 15` arm in
     * StatusMsg_Push is dead code under every path. */
    Check(st.nCount < PUFO_STATUS_MAX,
          "  the declared 15-record capacity is never reached");
}

/* The busy cap: with busy set, the count also stops at 3, but for a different
 * reason -- the message is DROPPED instead of replacing the oldest. */
static void TestBusyCapsAtThree(void)
{
    StatusStack st;

    printf("StatusMsg_Push -- a busy game caps the stack at 3\n");
    memset(&st, 0, sizeof(st));
    HsReset();
    g_hs.bBusy = true;

    for (i32 i = 0; i < 20; ++i) {
        char sz[32];

        snprintf(sz, sizeof(sz), "busy%d", i);
        (void)StatusMsg_Push(&st, sz, true, &g_pushHooks);
    }
    CheckEqI32(st.nCount, PUFO_STATUS_APPEND_MAX,
               "  twenty forced pushes while busy still leave exactly 3");
    /* Here the first three are the ones kept: busy drops rather than sliding. */
    CheckEqStr(st.aszMsg[0], "busy0", "  the first three are the ones kept");
    CheckEqStr(st.aszMsg[2], "busy2", "  and nothing after them got in");
}

/* The contrast between the two above is the whole point of the busy flag: it
 * decides whether a full stack SLIDES (not busy) or DROPS (busy). */
static void TestBusyChangesSlideToDrop(void)
{
    StatusStack st;

    printf("StatusMsg_Push -- busy turns a slide into a drop\n");
    memset(&st, 0, sizeof(st));
    HsReset();
    g_hs.bBusy = false;
    for (i32 i = 0; i < 3; ++i) {
        char sz[32];

        snprintf(sz, sizeof(sz), "a%d", i);
        (void)StatusMsg_Push(&st, sz, true, &g_pushHooks);
    }

    /* Not busy: the new message replaces the oldest. */
    (void)StatusMsg_Push(&st, "slide", true, &g_pushHooks);
    CheckEqStr(st.aszMsg[0], "a1", "  not busy: oldest slid out");
    CheckEqStr(st.aszMsg[2], "slide", "  not busy: newest got in");

    /* Busy: the same push is dropped, so the window is unchanged. */
    memset(&st, 0, sizeof(st));
    g_hs.bBusy = false;
    for (i32 i = 0; i < 3; ++i) {
        char sz[32];

        snprintf(sz, sizeof(sz), "b%d", i);
        (void)StatusMsg_Push(&st, sz, true, &g_pushHooks);
    }
    g_hs.bBusy = true;
    Check(!StatusMsg_Push(&st, "dropped", true, &g_pushHooks), "  busy: dropped");
    CheckEqStr(st.aszMsg[0], "b0", "  busy: oldest still there");
    CheckEqStr(st.aszMsg[2], "b2", "  busy: newest unchanged");
}

static void TestRenderFlag(void)
{
    StatusStack st;

    printf("StatusMsg_Push -- the repaint is gated on the display flag\n");
    memset(&st, 0, sizeof(st));
    HsReset();
    g_hs.bShouldRender = false;
    (void)StatusMsg_Push(&st, "x", true, &g_pushHooks);
    CheckEqI32(g_hs.nRenders, 0, "  no repaint when the flag is clear");

    HsReset();
    g_hs.bShouldRender = true;
    (void)StatusMsg_Push(&st, "y", true, &g_pushHooks);
    CheckEqI32(g_hs.nRenders, 1, "  repaint when the flag is set");

    /* And the stored message survives regardless of the render flag. */
    CheckEqStr(st.aszMsg[st.nCount - 1], "y", "  the message was still stored");
}

static void TestShiftOnly(void)
{
    StatusStack st;

    printf("StatusMsg_Shift\n");
    memset(&st, 0, sizeof(st));
    /* StatusMsg_Shift @ 0x2dd20: the count read at 0x2DD24 with `CMP #0` / `BEQ` at 0x2DD28/0x2DD2C guards
     * everything, and the decrement (`SUB R3,R3,#1` + `STR [R0,#0x54]` at 0x2DD98/0x2DD9C) runs whether or
     * not the copy ran. The copy starts only when the count is above 1 (`CMP #1` / `BLE` at
     * 0x2DD34/0x2DD38) and walks up from record 1 with its counter test at 0x2DD8C/0x2DD90: each aux
     * dword moves by `LDR R3,[R1,R3]` / `STR R3,[R1,R11]` at 0x2DD58/0x2DD60, and each 100-byte string
     * moves byte for byte by `LDRSB R5,[R3]` / `STRB R5,[R3,#-0x64]` at 0x2DD68/0x2DD6C -- the byte loop
     * stops after it has copied the NUL, so the terminator travels with the text. */
    StatusMsg_Shift(&st);
    CheckEqI32(st.nCount, 0, "  shift on an empty stack is a no-op @ 0x2dd20");

    memset(&st, 0, sizeof(st));
    HsReset();
    (void)StatusMsg_Push(&st, "a", true, &g_pushHooks);
    (void)StatusMsg_Push(&st, "b", true, &g_pushHooks);
    st.anTtl[0] = 11;
    st.anTtl[1] = 22;
    StatusMsg_Shift(&st);
    CheckEqI32(st.nCount, 1, "  count decremented once @ 0x2dd20");
    CheckEqStr(st.aszMsg[0], "b", "  the newest string moved down one slot @ 0x2dd20");
    CheckEqI32(st.anTtl[0], 22, "  the aux dword moved with its string @ 0x2dd20");
    CheckEqI32(st.anTtl[1], 22, "  ... and the source slot is left holding it, not zeroed @ 0x2dd20");

    StatusMsg_Shift(&st);
    CheckEqI32(st.nCount, 0, "  shift to empty, and no copy below count 1 @ 0x2dd20");
}

static void TestDraw(void)
{
    StatusStack st;

    printf("StatusMsg_DrawStack -- newest first, base y 320\n");
    memset(&st, 0, sizeof(st));
    HsReset();
    (void)StatusMsg_Push(&st, "old", true, &g_pushHooks);
    (void)StatusMsg_Push(&st, "mid", true, &g_pushHooks);
    (void)StatusMsg_Push(&st, "new", true, &g_pushHooks);

    {
        i32 nDrawY = 0;
        i32 nDrawn = StatusMsg_DrawStack(&st, &nDrawY, &g_drawHooks);

        /* StatusMsg_DrawStack @ 0x1afc4 packs the colour pair once, unconditionally, at the top of the
         * function; writes nStatusDrawY = 320 before any of the four conditions (only a player-input
         * enabled + selected unit + operative + not-busy chain replaces it with the inventory pad's own
         * y); then subtracts 14 and walks the records newest-first at x = 5, 12 pixels apart. */
        CheckEqI32(nDrawn, 3, "  three records drawn, newest first @ 0x1afc4");
        CheckEqI32(g_hs.nColors, 1, "  the colour pair is packed once, whatever the count @ 0x1afc4");
        /* Base: 320 - 14 = 306. Then 12 per already-drawn record. */
        CheckEqI32(nDrawY, 306, "  base y is 320 - 14 @ 0x1afc4");
        CheckEqI32(g_hs.anDrawX[0], PUFO_STATUS_DRAW_X, "  x is 5");
        CheckEqI32(g_hs.anDrawY[0], 306, "  first record at y 306");
        CheckEqI32(g_hs.anDrawY[1], 294, "  second at 306 - 12");
        CheckEqI32(g_hs.anDrawY[2], 282, "  third at 306 - 24");
        /* Newest first. */
        CheckEqStr(g_hs.aDrawn[0], "new", "  newest drawn first");
        CheckEqStr(g_hs.aDrawn[1], "mid", "  then mid");
        CheckEqStr(g_hs.aDrawn[2], "old", "  then oldest");
    }

    /* The hud-pad branch needs ALL four conditions. */
    HsReset();
    g_hs.bInputEnabled = true;
    g_hs.bOperative = true;
    g_hs.bBusy = false;
    g_hs.nHudPadY = 250;
    {
        i32 nDrawY = 0;

        (void)StatusMsg_DrawStack(&st, &nDrawY, &g_drawHooks);
        CheckEqI32(nDrawY, 250 - 14, "  hud-pad base used when all conditions hold");
    }

    /* Any one condition failing reverts to 320. */
    g_hs.bBusy = true;
    {
        i32 nDrawY = 0;

        (void)StatusMsg_DrawStack(&st, &nDrawY, &g_drawHooks);
        CheckEqI32(nDrawY, 306, "  busy reverts the base to 320 - 14");
    }
    g_hs.bBusy = false;
    g_hs.bOperative = false;
    {
        i32 nDrawY = 0;

        (void)StatusMsg_DrawStack(&st, &nDrawY, &g_drawHooks);
        CheckEqI32(nDrawY, 306, "  non-operative reverts the base");
    }
    g_hs.bOperative = true;
    g_hs.bInputEnabled = false;
    {
        i32 nDrawY = 0;

        (void)StatusMsg_DrawStack(&st, &nDrawY, &g_drawHooks);
        CheckEqI32(nDrawY, 306, "  input disabled reverts the base");
    }

    /* An empty stack draws nothing but still packs the colour. */
    {
        StatusStack empty;

        memset(&empty, 0, sizeof(empty));
        HsReset();
        {
            i32 nDrawY = 0;

            CheckEqI32(StatusMsg_DrawStack(&empty, &nDrawY, &g_drawHooks), 0,
                       "  nothing drawn from an empty stack");
            CheckEqI32(g_hs.nColors, 1, "  the colour is still packed");
            CheckEqI32(nDrawY, 306, "  the base y is still computed");
        }
    }
}

static void TestCapacityConstants(void)
{
    printf("Capacity constants match the original's guards\n");
    CheckEqI32(PUFO_STATUS_MAX, 15, "  15 records");
    CheckEqI32(PUFO_STATUS_REC_BYTES, 100, "  100 bytes per record");
    CheckEqI32(PUFO_STATUS_MAX * PUFO_STATUS_REC_BYTES, 1500,
               "  15 * 100 = the 1500-byte array");
    CheckEqI32(PUFO_STATUS_APPEND_MAX, 3, "  the append threshold is 3");
    CheckEqI32(PUFO_STATUS_TTL, 90, "  TTL is 90 frames");
}

int main(void)
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);

    TestPushSimple();
    TestBusyDrops();
    TestFullBuffer();
    TestBusyCapsAtThree();
    TestBusyChangesSlideToDrop();
    TestRenderFlag();
    TestShiftOnly();
    TestDraw();
    TestCapacityConstants();

    printf("\n%s: %d failure(s)\n", g_nFailures == 0 ? "PASS" : "FAIL", g_nFailures);
    return g_nFailures == 0 ? 0 : 1;
}
