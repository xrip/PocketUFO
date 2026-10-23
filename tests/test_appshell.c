/* test_appshell.c - the shell layer's own two records: WinMain @ 0x9fcc4 and PocketUFO_WndProc @ 0xa03ec.
 *
 * Evidence: WinMain @ 0x9fcc4 (132 bytes: 0x9FCDC InitInstance gate, 0x9FCE4/0x9FCEC LoadAcceleratorsW from
 * table 3, 0x9FD38 the GetMessageW loop, 0x9FD0C TranslateAcceleratorW FIRST with the
 * TranslateMessage/DispatchMessageW pair at 0x9FD14/0x9FD1C only when it answers 0, 0x9FD44 `return
 * Msg.wParam`), and PocketUFO_WndProc @ 0xa03ec (2964 bytes, 145 blocks, the two message ranges at 0xA0404
 * and 0xA06E0 -- the full map is in the IDB head note there).
 *
 * Both were unreachable until now: app/pufo_app.c is built as the pufo_app library and this test links it,
 * so it calls the executable's own pump and window procedure. src/pufo/app/pufo_main.c only forwards
 * wWinMain to App_Run. The port functions are App_PumpMessages (the pump alone) and PocketUFO_WndProc.
 *
 * What this test does NOT run, on purpose: the 0xC351 frame arm and the 0xC353 pulse arm need the world
 * bring-up (assets, the geoscape frame wiring) that the frame and e2e gates already build, and the key arms
 * need the hook table App_Run installs. Here the world block stays at its static state, so every arm that
 * runs is one whose effect is visible in a cell of that block, and what the test pins is the DISPATCH --
 * which body a given (msg, wParam) selects, and what the handler answers.
 *
 * The window the pump dispatches to is a test-owned class. The WM_COMMAND and WM_DESTROY checks use a
 * window whose procedure really is PocketUFO_WndProc, because those arms end in SendMessageW /
 * DefWindowProcW / PostQuitMessage and only a real HWND shows what they do.
 */
#include "app/pufo_app.h"
#include "app/pufo_world.h"
#include "game/pufo_timer.h"

#include <stdio.h>
#include <string.h>

#include <windows.h>

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

static void DrainQueue(void)
{
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        /* App_QuitDisplay's PostQuitMessage(0) and any leftover posted message: this thread's queue has to
         * be empty before the next test pumps, or GetMessageW answers the old WM_QUIT first. */
    }
}

/* Measured (build/crops/peek_quit.c): PeekMessageW with PM_REMOVE really does report a WM_QUIT -- it answers
 * nonzero with msg.message 0x12 and wParam 0, and the quit is gone afterwards -- while GetMessageW swallows
 * it into a FALSE return. That is what lets a test prove App_QuitDisplay ran without pumping, and pumping is
 * only ever done where a quit is known to be queued: GetMessageW blocks on an empty queue, and a test that
 * blocks is worse than a test that fails. */
static bool QuitInQueue(void)
{
    MSG msg;

    for (int i = 0; i < 16; ++i) {
        if (!PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            return false; /* the queue is empty: no quit was posted */
        }
        if (msg.message == WM_QUIT) {
            return true;
        }
    }
    return false;
}

/* ---- a window that records what it was told ------------------------------------------------------ */

#define REC_MAX 16

typedef struct Rec {
    UINT   uMsg;
    WPARAM wParam;
} Rec;

static Rec  g_aRec[REC_MAX];
static int  g_nRec;

static LRESULT CALLBACK RecProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    if (g_nRec < REC_MAX) {
        g_aRec[g_nRec].uMsg   = uMsg;
        g_aRec[g_nRec].wParam = wParam;
        ++g_nRec;
    }
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static bool MakeWindow(const wchar_t *pszClass, WNDPROC pfnProc, HWND *phWnd)
{
    WNDCLASSW wc;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc   = pfnProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.lpszClassName = pszClass;
    if (RegisterClassW(&wc) == 0) {
        *phWnd = NULL;
        return false;
    }
    *phWnd = CreateWindowExW(0, pszClass, L"pufo test window", 0, 0, 0, 8, 8, NULL, NULL,
                             GetModuleHandleW(NULL), NULL);
    return *phWnd != NULL;
}

static bool RecHas(UINT uMsg, WPARAM wParam)
{
    for (int i = 0; i < g_nRec; ++i) {
        if (g_aRec[i].uMsg == uMsg && g_aRec[i].wParam == wParam) {
            return true;
        }
    }
    return false;
}

static bool RecHasCommandLow1(void)
{
    for (int i = 0; i < g_nRec; ++i) {
        if (g_aRec[i].uMsg == WM_COMMAND && (u16)g_aRec[i].wParam == 1u) {
            return true;
        }
    }
    return false;
}

/* ---- 1. the pump: accelerator first, MSG.wParam out --------------------------------------------- */

static void TestPumpAcceleratorFirst(void)
{
    HACCEL hAccel;
    ACCEL  aTable[1];
    HWND   hWnd;
    int    nRet;

    printf("WinMain @ 0x9fcc4 -- the pump: TranslateAcceleratorW first, then translate/dispatch, MSG.wParam out\n");

    if (!MakeWindow(L"PUFO_TEST_PUMP", RecProc, &hWnd)) {
        Check(false, "a window to dispatch to (test cannot continue)");
        return;
    }

    /* The record loads the table by id: `MOV R1,#3` at 0x9FCE4, `BL LoadAcceleratorsW` at 0x9FCEC. This test
     * builds the table with CreateAcceleratorTableW instead -- VK_F5 -> command 1, and 1 is exactly the
     * command value PocketUFO_WndProc's own arm compares at 0xA0CA4. */
    /* tagACCEL is { fVirt, key, cmd } -- winuser.h 3177: "fVirt ... also called the flags field". */
    memset(aTable, 0, sizeof(aTable));
    aTable[0].fVirt = FVIRTKEY;
    aTable[0].key   = VK_F5;
    aTable[0].cmd   = 1;
    hAccel = CreateAcceleratorTableW(aTable, 1);
    Check(hAccel != NULL, "  CreateAcceleratorTableW gave the pump a real table to translate with");

    g_nRec = 0;
    (void)PostMessageW(hWnd, WM_KEYDOWN, VK_F5, 0); /* matched by the table: the key never reaches the window */
    (void)PostMessageW(hWnd, WM_KEYDOWN, VK_F9, 0); /* not matched: the translate/dispatch pair runs instead   */
    (void)PostMessageW(hWnd, WM_QUIT, 7, 0);        /* stops the loop and IS the exit code                     */

    nRet = App_PumpMessages(hAccel);

    for (int i = 0; i < g_nRec; ++i) {
        printf("       dispatched %d: msg 0x%X wParam 0x%X\n", i, (unsigned)g_aRec[i].uMsg, (unsigned)g_aRec[i].wParam);
    }

    /* Measured here, and it is the reason for the record's odd-looking half-test: the WM_COMMAND an
     * accelerator produces carries its SOURCE id in the HIGH word -- VK_F5 with cmd 1 arrived as WM_COMMAND
     * with wParam 0x10001. 0xA0C9C compares only the low half (`MOV R3,R0,LSL#16` / `MOV R3,R3,LSR#16` /
     * `CMP R3,#1`) because a menu-sourced WM_COMMAND carries 0 there and an accelerator-sourced one carries
     * 1, and both must land in the same arm. A system broadcast (0x031F) also turned up in the queue, so
     * nothing here counts the messages -- every check names the message it looks for. */
    Check(RecHasCommandLow1(),
          "  the matched key came back as WM_COMMAND whose LOW half is the table's command id: TranslateAcceleratorW runs FIRST @ 0x9FD0C");
    Check(!RecHas(WM_KEYDOWN, VK_F5),
          "  and the key itself was consumed before DispatchMessageW -- the window never sees it @ 0x9FD0C");
    Check(!RecHas(WM_QUIT, 7), "  the WM_QUIT was dispatched to nobody: 0x9FD38 tests it and the loop leaves @ 0x9fcc4");
    Check(RecHas(WM_KEYDOWN, VK_F9),
          "  the unmatched key went the other way: TranslateAcceleratorW answered 0 and the pair ran @ 0x9FD14/0x9FD1C");
    CheckEqI32(nRet, 7, "  the pump's return value is the WM_QUIT's own wParam @ 0x9FD44");

    (void)DestroyAcceleratorTable(hAccel);
    (void)DestroyWindow(hWnd);
    (void)UnregisterClassW(L"PUFO_TEST_PUMP", GetModuleHandleW(NULL));
    DrainQueue();
}

/* ---- 2. WM_TIMER: which body an id selects ------------------------------------------------------ */

static void TestTimerIdDispatch(void)
{
    PufoWorldState *pW = App_WorldState();
    LRESULT         lRet;

    printf("PocketUFO_WndProc @ 0xa03ec -- WM_TIMER: 0xA08B0's gate and the 0xA0944 switch\n");

    /* 0xC352, the anim arm. Its first store is the pair at 0xA0968/0xA0978 -- `MOV R1,#1` then
     * STRB into bPanelFlagScratch, unconditional in the original's timer body -- which the port keeps as
     * the world's bPlaneRowFlag. */
    pW->bPlaneRowFlag  = 0;
    pW->cUiAnimProgress = 0;
    lRet = PocketUFO_WndProc(NULL, WM_TIMER, PUFO_TIMER_ANIM, 0);
    CheckEqI32(pW->bPlaneRowFlag, 1, "  0xC352 stores the panel flag the 0xA0968 pair stores @ 0xa03ec");
    CheckEqI32((i32)lRet, 0, "  a WM_TIMER firing answers 0 @ 0xa03ec");

    /* 0xC354, the anim-progress arm: `if (cUiAnimProgress < 101) ++` (0xA0C6C..0xA0C90). */
    (void)PocketUFO_WndProc(NULL, WM_TIMER, PUFO_TIMER_EXTRA, 0);
    CheckEqI32(pW->cUiAnimProgress, 1, "  0xC354 advances the anim-progress byte @ 0xa03ec");
    pW->cUiAnimProgress = 101;
    (void)PocketUFO_WndProc(NULL, WM_TIMER, PUFO_TIMER_EXTRA, 0);
    CheckEqI32(pW->cUiAnimProgress, 101, "  and the `< 101` test at 0xA0C6C stops it there @ 0xa03ec");

    /* 0xC351 (the render tick, 0xA094C), 0xC350 (the game tick, behind the `wParam != 50000` gate at
     * 0xA08B0) and an id no arm claims. None of them belongs to the anim bodies, and the game-tick arm is
     * Timer_TickBySpeed -> GameTick_OnTimer, which on this world takes gate 1 (no hold), gate 2
     * (bGeoscapeUi clear) and gate 4 (bTactFlag clear) and returns -- see tests/test_timer.c for the speed
     * table itself. */
    pW->bPlaneRowFlag   = 0;
    pW->cUiAnimProgress = 0;
    (void)PocketUFO_WndProc(NULL, WM_TIMER, PUFO_TIMER_RENDER, 0);
    CheckEqI32(pW->bPlaneRowFlag, 0, "  0xC351 is the frame arm, not the anim arm: the flag stays clear @ 0xa03ec");
    (void)PocketUFO_WndProc(NULL, WM_TIMER, PUFO_TIMER_GAMETICK, 0);
    CheckEqI32(pW->cUiAnimProgress, 0,
               "  50000 (= 0xC350) stays behind 0xA08B0's gate and never reaches the 0xC351..0xC354 switch @ 0xa03ec");
    (void)PocketUFO_WndProc(NULL, WM_TIMER, 0xC399, 0);
    CheckEqI32(pW->bPlaneRowFlag, 0, "  an id no arm claims moves nothing @ 0xa03ec");
    CheckEqI32(pW->cUiAnimProgress, 0, "  and it moves nothing else either @ 0xa03ec");

    DrainQueue();
}

/* ---- 3. the other arms and the DefWindowProcW fall-through -------------------------------------- */

static void TestMessageArms(void)
{
    HWND hWnd;

    printf("PocketUFO_WndProc @ 0xa03ec -- the message arms and 0xA0740's DefWindowProcW tail\n");

    if (!MakeWindow(L"PUFO_TEST_ARMS", PocketUFO_WndProc, &hWnd)) {
        Check(false, "a window whose procedure is the port's own (test cannot continue)");
        return;
    }

    /* WM_CREATE, 0xA06A4: the AYGSHELL activation block (memset of 12 bytes, cbSize 12) -- the port's
     * documented stand-in, and the arm answers 0. */
    CheckEqI32((i32)PocketUFO_WndProc(hWnd, WM_CREATE, 0, 0), 0, "  WM_CREATE answers 0 @ 0xa03ec");

    /* WM_SIZE is 5, and the record's answer to msg == 5 is a plain `return 0` -- no arm body, no paint. */
    CheckEqI32((i32)PocketUFO_WndProc(hWnd, WM_SIZE, 0, 0), 0, "  msg 5 (WM_SIZE) answers 0, the record's own answer @ 0xa03ec");

    /* WM_ERASEBKGND is 0x14: the low switch at 0xA0408 has no arm for it, so the record hands it to
     * DefWindowProcW. The port answers 1 because its surface covers the client area -- a deliberate
     * deviation, named in docs/parity/bugs.tsv, and pinned here so it cannot drift further. */
    CheckEqI32((i32)PocketUFO_WndProc(hWnd, WM_ERASEBKGND, 0, 0), 1,
               "  WM_ERASEBKGND: the port answers 1 where the record falls through to DefWindowProcW (deviation) @ 0xa03ec");
    Check((i32)PocketUFO_WndProc(hWnd, WM_ERASEBKGND, 0, 0) != (i32)DefWindowProcW(hWnd, WM_ERASEBKGND, 0, 0),
          "  and the two answers really differ -- the record's path is not this one @ 0xa03ec");

    /* An unclaimed message must answer exactly what DefWindowProcW answers. */
    CheckEqI32((i32)PocketUFO_WndProc(hWnd, WM_NCHITTEST, 0, 0), (i32)DefWindowProcW(hWnd, WM_NCHITTEST, 0, 0),
               "  an unclaimed message returns DefWindowProcW's own value @ 0xa03ec");

    /* The key arms. VK_F9 is in none of the eight key-map slots (0x1884D8 stride 12: up, down, left, right,
     * action, secondary, confirm, escape -- the port's PC codes are VK_UP/DOWN/LEFT/RIGHT/RETURN/SPACE/
     * BACK/ESCAPE), so no arm body runs; the arm still answers 0 at 0xA0E1C / 0xA0D00, which is the reason
     * DefWindowProcW never sees a game key and a game key therefore produces no character. The mapped-key
     * bodies belong to tests/test_input.c and need App_Run's hook table. */
    CheckEqI32((i32)PocketUFO_WndProc(hWnd, WM_KEYDOWN, VK_F9, 0), 0, "  an unmatched WM_KEYDOWN still answers 0 @ 0xA0E1C");
    CheckEqI32((i32)PocketUFO_WndProc(hWnd, WM_KEYUP, VK_F9, 0), 0, "  and so does an unmatched WM_KEYUP @ 0xA0D00");

    (void)DestroyWindow(hWnd);
    (void)UnregisterClassW(L"PUFO_TEST_ARMS", GetModuleHandleW(NULL));
    DrainQueue();
}

/* ---- 4. WM_COMMAND 1: the window's own quit ----------------------------------------------------- */

static void TestCommandOneClosesTheWindow(void)
{
    HWND    hWnd;
    LRESULT lRet;

    printf("PocketUFO_WndProc @ 0xa03ec -- WM_COMMAND 1 re-enters the window: WM_ACTIVATE, then WM_CLOSE\n");

    if (!MakeWindow(L"PUFO_TEST_CMD", PocketUFO_WndProc, &hWnd)) {
        Check(false, "a window whose procedure is the port's own (test cannot continue)");
        return;
    }

    /* 0xA0C9C..0xA0CA4: `MOV R3,R0,LSL#16` / `MOV R3,R3,LSR#16` / `CMP R3,#1` -- the LOW half of wParam.
     * Command 1 then runs SendMessageW(hWnd, WM_ACTIVATE, 0, hWnd) at 0xA0CB0 (the lParam slot really gets
     * the hwnd: R4, saved from R0 at 0xA03F0) and SendMessageW(hWnd, WM_CLOSE, 0, 0) at 0xA0CC4, and
     * returns 0 at 0xA0F74. WM_CLOSE has no arm of its own, so DefWindowProcW destroys the window and
     * WM_DESTROY (0xA069C -> App_QuitDisplay @ 0xa0388) runs. */
    lRet = PocketUFO_WndProc(hWnd, WM_COMMAND, 1, 0);
    CheckEqI32((i32)lRet, 0, "  command 1 answers 0 @ 0xa03ec");
    if (IsWindow(hWnd)) {
        /* The quit never happened, so the pump must NOT be run here: GetMessageW blocks on an empty queue
         * and the test would hang instead of reporting. A mutation that removes the WM_CLOSE at 0xA0CC4
         * lands on this line and still prints a failing check. */
        Check(false, "  the WM_CLOSE at 0xA0CC4 destroyed the window: command 1 is the window's own quit @ 0xa03ec");
        Check(false, "  (pump skipped: nothing posted a quit, so GetMessageW would block forever) @ 0xa03ec");
    } else {
        Check(true, "  the WM_CLOSE at 0xA0CC4 destroyed the window: command 1 is the window's own quit @ 0xa03ec");

        /* App_QuitDisplay's PostQuitMessage(0) is the observable half of the chain. GetMessageW BLOCKS when
         * the queue is empty, so a pump that returns at all proves a message was there, and a return of 0
         * proves it is the App_QuitDisplay quit rather than a stray broadcast (the pump dispatches those and
         * keeps going). PeekMessageW is not usable as the proof: measured here, the same queued WM_QUIT
         * answered FALSE in one test and came back as a different message in another. */
        CheckEqI32(App_PumpMessages(NULL), 0,
                   "  WM_DESTROY ran App_QuitDisplay: the pump finds its quit and answers with its wParam @ 0xa0388");
    }
    DrainQueue();

    /* Any other command value jumps to 0xA0740: `MOV R3,R2 / MOV R2,R0 / MOV R0,R4 / BL DefWindowProcW`
     * with R1 left at 0x111 by 0xA0CA8's `MOVNE`. A command that is not 1 must therefore answer exactly
     * what DefWindowProcW answers to WM_COMMAND. */
    {
        HWND hOther;

        if (MakeWindow(L"PUFO_TEST_CMD2", PocketUFO_WndProc, &hOther)) {
            CheckEqI32((i32)PocketUFO_WndProc(hOther, WM_COMMAND, 42, 0),
                       (i32)DefWindowProcW(hOther, WM_COMMAND, 42, 0),
                       "  a command that is not 1 takes the DefWindowProcW tail at 0xA0740 @ 0xa03ec");
            (void)DestroyWindow(hOther);
            (void)UnregisterClassW(L"PUFO_TEST_CMD2", GetModuleHandleW(NULL));
        }
    }
}

/* ---- 5. WM_DESTROY straight through ------------------------------------------------------------- */

static void TestDestroyArm(void)
{
    HWND hWnd;

    printf("PocketUFO_WndProc @ 0xa03ec -- WM_DESTROY (2) answers App_QuitDisplay\n");

    if (!MakeWindow(L"PUFO_TEST_DESTROY", PocketUFO_WndProc, &hWnd)) {
        Check(false, "a window whose procedure is the port's own (test cannot continue)");
        return;
    }

    CheckEqI32((i32)PocketUFO_WndProc(hWnd, WM_DESTROY, 0, 0), 0, "  WM_DESTROY answers 0 @ 0xa03ec");
    Check(QuitInQueue(),
          "  and the arm ran App_QuitDisplay: its PostQuitMessage(0) is in the queue @ 0xa0388");
    DrainQueue();
    (void)DestroyWindow(hWnd);
    (void)UnregisterClassW(L"PUFO_TEST_DESTROY", GetModuleHandleW(NULL));
}

int main(void)
{
    DrainQueue();
    TestPumpAcceleratorFirst();
    TestTimerIdDispatch();
    TestMessageArms();
    TestCommandOneClosesTheWindow();
    TestDestroyArm();

    if (g_nFailures > 0) {
        printf("test_appshell: %d check(s) FAILED\n", g_nFailures);
        return 1;
    }
    printf("test_appshell: all checks passed\n");
    return 0;
}
