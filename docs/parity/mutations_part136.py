#!/usr/bin/env python3
"""Part 136: mutations for the shell's own two records -- WinMain @ 0x9fcc4 and PocketUFO_WndProc @ 0xa03ec.

Every case breaks the DISPATCH, which is what tests/test_appshell.c pins: which body a (msg, wParam) pair
selects, what the handler answers, and what the pump does with the message queue.

src/pufo/app/pufo_app.c is CRLF-only (measured: 2465 CRLF, 0 bare LF), so every anchor here is written with
\n and converted by crlf() before it is matched.

Not covered, and why: the record's `GetMessageW(...) != 0` test at 0x9FD38 cannot be pinned by a mutation --
measured in this test, GetMessageW answers 0 for the WM_QUIT and never answered negative, so `!= 0` and
`> 0` are the same program for every message the suite can produce. That is recorded in PLAN.md rather than
faked with a check. The same is true of the game-tick arm's own effect: on the shell's idle world
GameTick_OnTimer takes gate 1 (no hold), gate 2 (bGeoscapeUi clear) and gate 4 (bTactFlag clear) and changes
nothing, so what the 0xA08B0 gate pins here is that 0xC350 does NOT reach the anim bodies (case 6), and the
tick count itself stays pinned by tests/test_timer.c at 0xA08C0..0xA0934.

Run from the repo root:  python -I docs/parity/mutations_part136.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from mutate_run import run_case

A = "src/pufo/app/pufo_app.c"
T = "test_appshell"


def crlf(text):
    return text.replace("\n", "\r\n").encode("utf-8")


# ---- 1..3: the pump, WinMain @ 0x9fcc4 (0x9FD04..0x9FD44) ----------------------------------------

PUMP_BODY = crlf("""        if (TranslateAcceleratorW(msg.hwnd, hAccel, &msg) == 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }""")

# 1. no accelerator step at all: every message goes to the window unchanged.
run_case("the pump never calls TranslateAcceleratorW", T, A, PUMP_BODY, crlf("""        (void)hAccel; /* MUTATION: 0x9FD0C's accelerator step is gone */
        TranslateMessage(&msg);
        DispatchMessageW(&msg);"""))

# 2. the pair runs FIRST: the key reaches the window before the accelerator converts it.
run_case("the pump dispatches before it translates accelerators", T, A, PUMP_BODY, crlf("""        /* MUTATION: the 0x9FD14/0x9FD1C pair before the 0x9FD0C test */
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        (void)TranslateAcceleratorW(msg.hwnd, hAccel, &msg);"""))

# 3. 0x9FD44 `return Msg.wParam` replaced by a fixed 0.
run_case("the pump answers 0 instead of the WM_QUIT's wParam", T, A, crlf("""    return (int)msg.wParam;
}"""), crlf("""    return 0; /* MUTATION: 0x9FD44's Msg.wParam dropped */
}"""))

# ---- 4..6: the WM_TIMER dispatch (0xA08B0 gate, 0xA0944 switch) ---------------------------------

# 4. the two arm bodies swapped: 0xC351 gets the anim body and 0xC352 gets the frame body.
run_case("the 0xC351 and 0xC352 arms trade bodies", T, A, crlf("""        } else if (wParam == PUFO_TIMER_ANIM) {
            App_TickAnim();
        } else if (wParam == PUFO_TIMER_RENDER) {
            App_DrawFrame();"""), crlf("""        } else if (wParam == PUFO_TIMER_ANIM) {
            App_DrawFrame(); /* MUTATION: swapped with 0xC351 */
        } else if (wParam == PUFO_TIMER_RENDER) {
            App_TickAnim();"""))

# 5. the anim-progress body moved onto an id the record gives no body.
run_case("the anim-progress body answers 0xC399 instead of 0xC354", T, A, crlf("""        } else if (wParam == PUFO_TIMER_EXTRA) {
            App_TickAnimProgress();"""), crlf("""        } else if (wParam == 0xC399) { /* MUTATION: not 0xC354 (0xA0C6C) */
            App_TickAnimProgress();"""))

# 6. 0xA08B0's game-tick branch removed, with 0xC350 folded into the anim arm: 50000 now stores the anim
#    arm's byte, which the test reads as "an id no arm claims moves nothing".
run_case("the 0xA08B0 gate is gone and 0xC350 takes the anim arm", T, A, crlf("""        if (wParam == PUFO_TIMER_GAMETICK) {
            /* The original's gate at 0xA08B0 keeps 0xC350 out of that switch: the speed decides the tick
             * count (Timer_TickBySpeed, 0xA08C0..0xA0934) and App_GameTickOnce is this shell's callee. */
            Timer_TickBySpeed((i32)g_world.gameClock.nSpeed, App_GameTickOnce, NULL);
        } else if (wParam == PUFO_TIMER_ANIM) {"""), crlf("""        if (wParam == PUFO_TIMER_ANIM || wParam == PUFO_TIMER_GAMETICK) { /* MUTATION: no 0xA08B0 gate */"""))

# ---- 7..9: the WM_COMMAND arm (0xA0C9C..0xA0CDC) and the DefWindowProcW fall-through -----------

# 7. the gate tests command 2 instead of 1: command 1 then falls to 0xA0740.
run_case("the WM_COMMAND gate tests 2 instead of 1", T, A,
         crlf("        if ((u16)wParam == 1u) {"),
         crlf("        if ((u16)wParam == 2u) { /* MUTATION: 0xA0CA4 compares 1 */"))

# 8. the arm sends WM_ACTIVATE but not the WM_CLOSE at 0xA0CC4: the window survives.
run_case("the WM_COMMAND arm drops its WM_CLOSE", T, A,
         crlf("            (void)SendMessageW(hWnd, WM_CLOSE, 0, 0);"),
         crlf("            /* MUTATION: 0xA0CC4's WM_CLOSE not sent */"))

# 9. WM_ERASEBKGND takes the record's own path (0xA0740's DefWindowProcW) instead of the port's 1.
run_case("WM_ERASEBKGND falls through to DefWindowProcW", T, A,
         crlf("        return 1; /* the surface covers the client area */"),
         crlf("        break; /* MUTATION: 0x14 has no arm in the record's low switch */"))
