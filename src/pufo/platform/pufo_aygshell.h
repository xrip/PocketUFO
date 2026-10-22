/* pufo_aygshell.h - the AYGSHELL stand-in.
 *
 * Evidence: PocketUFO_WndProc @ 0xa03ec has three arms that belong to the Pocket PC shell.
 *   case 1 (WM_CREATE) memsets the 12-byte block at 0x188380 and stores cbSize = 12
 *     (0xA06A4: memset(psai, 0, 0xC); 0xA06C0: STR #0xC into [psai]).
 *   case 6 (WM_ACTIVATE) calls SHHandleWMActivate (0xA0690) with hwnd, wParam and that block.
 *   case 0x1A (WM_SETTINGCHANGE) calls SHHandleWMSettingChange (0xA06D0) with hwnd, wParam
 *     and that block.
 * All three return 0 through the common tail at 0xA0F74.
 *
 * What those two imports do on WinCE is shell bookkeeping: they remember whether the window
 * had already been activated (so the shell can skip the first suspend) and which settings key
 * last changed. Nothing in the game reads the answers back -- 0x188380 has no other reference
 * in the image, and the cell right after it is the timer scratch pufo_timer.h:207 names.
 *
 * Win32 has no counterpart and no AYGSHELL is linked. The decision recorded in PLAN.md (the
 * AYGSHELL trio row) is exactly what this file is: the arms and the block stay in the port, the
 * handlers are documented no-ops, and the platform deviation is stated here and in
 * docs/parity/scaffold.md.
 */
#ifndef PUFO_AYG_SHELL_H
#define PUFO_AYG_SHELL_H

#include "pufo_common.h"

#define PUFO_SH_ACTIVATE_INFO_SIZE 12u

/* SHACTIVATEINFO (aygshell.h): cbSize, fstate, cesFlags. */
typedef struct PufoShActivateInfo {
    u32 cbSize;    /* WM_CREATE stores 12 here (0xA06C0) */
    u32 fstate;    /* what the CE shell would keep: the window was already activated */
    u32 cesFlags;  /* and the last settings change it saw */
} PufoShActivateInfo;

/* psai @ 0x188380. */
extern PufoShActivateInfo g_shActInfo;

/* The WM_CREATE arm: memset + cbSize = 12. */
void Sh_ActivateInfoInit(void);

/* 0xA0690's callee, SHHandleWMActivate(hwnd, msg, wParam, lParam, psai). No-op stand-in. */
void Sh_HandleWmActivate(void *hWnd, u32 nState, const void *pLParam);

/* 0xA06D0's callee, SHHandleWMSettingChange(hwnd, wParam, psai). No-op stand-in. */
void Sh_HandleWmSettingChange(void *hWnd, u32 nFlags, const void *pKey);

#endif /* PUFO_AYG_SHELL_H */
