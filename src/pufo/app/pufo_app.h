/* pufo_app.h - the shell layer's own entry points.
 *
 * Evidence: WinMain @ 0x9fcc4 (App_Run and its pump App_PumpMessages), PocketUFO_WndProc @ 0xa03ec.
 *
 * The shell is a library, not exe-only code, for the same reason app/pufo_world.c is one: two of these
 * functions are original records the suite has to reach. tests/test_appshell.c calls the message pump and
 * the window procedure directly -- the pump's accelerator-first order and its exit code, and WndProc's
 * message/timer-id dispatch -- and nothing else in the tree can observe either, because both were the
 * executable's own code. src/pufo/app/pufo_main.c carries the entry point the CRT looks for.
 */
#ifndef PUFO_APP_H
#define PUFO_APP_H

#include "app/pufo_world.h"

#include <windows.h>

/* WinMain @ 0x9fcc4: the InitInstance gate, LoadAcceleratorsW from table 3, the pump, MSG.wParam out. */
int App_Run(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow);

/* The pump on its own, 0x9FD04..0x9FD44: TranslateAcceleratorW FIRST and only on 0 the
 * TranslateMessage / DispatchMessageW pair; the loop runs while GetMessageW answers non-zero (0x9FD38),
 * and the return value is the WM_QUIT message's own wParam (0x9FD44). */
int App_PumpMessages(HACCEL hAccel);

LRESULT CALLBACK PocketUFO_WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

/* SHELL SCAFFOLDING, not the original: the shell's own world block, so a test can see which timer arm a
 * WM_TIMER firing moved. The original has one Map block at 0x1139B8; this is the port's copy of it. */
PufoWorldState *App_WorldState(void);

/* The same setup steps App_Run uses: initialize its world owner and install the live input hooks. */
bool App_InitializeWorld(void);
void App_ShutdownWorld(void);
const TactInputHooks *App_InitTactInputHooks(void);

#endif /* PUFO_APP_H */
