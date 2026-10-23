/* pufo_main.c - the executable's only translation unit.
 *
 * The entry the CRT looks for. The port of WinMain @ 0x9fcc4 is App_Run in app/pufo_app.c, and the shell
 * layer is a library rather than exe-only code so that tests/test_appshell.c can call the pump
 * (App_PumpMessages) and the window procedure (PocketUFO_WndProc @ 0xa03ec) themselves -- the code this
 * executable runs, not a copy of it. See app/pufo_app.h.
 */
#include "app/pufo_app.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    return App_Run(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}
