/* pufo_aygshell.c - the AYGSHELL stand-in. See pufo_aygshell.h for the three arms
 * this reproduces and for why the two shell handlers do nothing here.
 */
#include "platform/pufo_aygshell.h"

#include <string.h>

PufoShActivateInfo g_shActInfo; /* psai @ 0x188380 */

void Sh_ActivateInfoInit(void)
{
    /* 0xA06A4: memset(psai, 0, 0xC); then cbSize = 0xC. The size field is the only
     * value the original ever writes from the port's point of view, because the two
     * handlers below are where the shell would have written the rest. */
    memset(&g_shActInfo, 0, sizeof(g_shActInfo));
    g_shActInfo.cbSize = PUFO_SH_ACTIVATE_INFO_SIZE;
}

void Sh_HandleWmActivate(void *hWnd, u32 nState, const void *pLParam)
{
    /* SHHandleWMActivate on WinCE folds the activation history into psai->fstate.
     * No game code reads psai (0x188380 has no other reference in the beta), so
     * there is nothing here to keep: the arm exists, the arguments are taken, and
     * no AYGSHELL is linked (PLAN.md's AYGSHELL row records the decision). */
    (void)hWnd;
    (void)nState;
    (void)pLParam;
}

void Sh_HandleWmSettingChange(void *hWnd, u32 nFlags, const void *pKey)
{
    /* SHHandleWMSettingChange on WinCE remembers the settings key in psai->cesFlags,
     * which is how a CE app restores the tray/auto-repeat state. The port has no such
     * state to restore and the block has no reader: documented no-op. */
    (void)hWnd;
    (void)nFlags;
    (void)pKey;
}
