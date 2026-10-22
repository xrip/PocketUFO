/* pufo_menu.c - the options-menu button handlers.
 *
 * Evidence: see pufo_menu.h for the address list and the per-group differences.
 */
#include "game/pufo_menu.h"

#include <string.h>

static void Menu_Play(const MenuHooks *pHooks, i32 nSample)
{
    if (pHooks != NULL && pHooks->pfnPlaySample != NULL) {
        pHooks->pfnPlaySample(nSample, pHooks->pUser);
    }
}

/* Every handler makes its group one-hot before doing anything else: clear all
 * five pads, then light the chosen one. */
static void Pads_MakeOneHot(u8 *pPads, i32 nStep)
{
    memset(pPads, 0, PUFO_MENU_STEPS);
    if (nStep >= 0 && nStep < PUFO_MENU_STEPS) {
        pPads[nStep] = 1;
    }
}

static bool ValidStep(i32 nStep)
{
    return nStep >= 0 && nStep < PUFO_MENU_STEPS;
}

/* MenuBtn_SetSpeed0 @ 0x18ecc, MenuBtn_SetSpeed1 @ 0x18f20, MenuBtn_SetSpeed2 @ 0x18f74,
 * MenuBtn_SetSpeed3 @ 0x18fc8, MenuBtn_SetSpeed4 @ 0x1901c. Verified 2026-10-22: five copies of one
 * 19-instruction body (84 bytes each) whose only difference is the level -- `Sound_Play(17)`,
 * a one-hot over the five speed pad globals 0xACE5A/0xACE7A/0xACE9A/0xACEBA/0xACEDA at the button's own
 * index, then `GameSpeed_Set(&Map_bTactFlag, nSpeed = index)`. */
bool Menu_SetSpeed(PufoOptions *pOpt, GameClock *pClock, MenuPads *pPads,
                   i32 nStep, const MenuHooks *pHooks)
{
    if (pOpt == NULL || pClock == NULL || pPads == NULL || !ValidStep(nStep)) {
        return false;
    }
    Menu_Play(pHooks, PUFO_MENU_SAMPLE_PRE);
    Pads_MakeOneHot(pPads->abSpeed, nStep);
    /* GameSpeed_Set both stores the speed and restarts the timers, so this is the
     * whole effect; the pads are the only extra state. */
    return GameSpeed_Set(pClock, (u32)nStep);
}

bool Menu_SetGamma(PufoOptions *pOpt, MenuPads *pPads, i32 nStep,
                   const MenuHooks *pHooks)
{
    if (pOpt == NULL || pPads == NULL || !ValidStep(nStep)) {
        return false;
    }
    Menu_Play(pHooks, PUFO_MENU_SAMPLE_PRE);
    Pads_MakeOneHot(pPads->abGamma, nStep);
    pOpt->nGamma = nStep;
    /* The palettes are rebuilt from the new gamma, which is why the caller must
     * re-upload them afterwards. */
    if (pHooks != NULL && pHooks->pfnLoadPalettes != NULL) {
        pHooks->pfnLoadPalettes(pHooks->pUser);
    }
    return true;
}

/* Options_SetMusicVol1 @ 0x19a10, Vol2 @ 0x19a64, Vol3 @ 0x19ab8, Vol4 @ 0x19b0c, Vol5 @ 0x19b60.
 * Verified 2026-10-22: five copies of one 18-instruction body (84 bytes each); the only difference is
 * the level -- the value
 * written to Map_nMusicVolume is 1..5 and the pad made one-hot is that value minus one. No
 * Sound_Play anywhere in this family (that is what separates it from the sfx group below). */
bool Menu_SetMusicVolume(PufoOptions *pOpt, MenuPads *pPads, i32 nStep,
                         const MenuHooks *pHooks)
{
    if (pOpt == NULL || pPads == NULL || !ValidStep(nStep)) {
        return false;
    }
    /* No sample in this group -- verified: Options_SetMusicVol1..5 contain no
     * Sound_Play call at all. The stored value is 1..5 (Vol1 writes the immediate
     * 1 at 0x19A20, Vol5 writes 5 at 0x19B70), the pad index is 0-based. */
    pOpt->nMusicVolume = nStep + 1;
    Pads_MakeOneHot(pPads->abMusic, nStep);
    if (pHooks != NULL && pHooks->pfnApplyMusicVolume != NULL) {
        pHooks->pfnApplyMusicVolume(pHooks->pUser);
    }
    return true;
}

/* MenuBtn_SetSfxVol1 @ 0x19bb4, Vol2 @ 0x19c14, Vol3 @ 0x19c74, Vol4 @ 0x19cd4, Vol5 @ 0x19d34.
 * Verified 2026-10-22: five copies of one 21-instruction body (96 bytes each), the level the only
 * difference -- store 1..5 into
 * Map_nSoundVolume, one-hot the pad at level-1, `Sound_ApplySfxVolume`, then `Sound_Play(17)` last. */
bool Menu_SetSfxVolume(PufoOptions *pOpt, MenuPads *pPads, i32 nStep,
                       const MenuHooks *pHooks)
{
    if (pOpt == NULL || pPads == NULL || !ValidStep(nStep)) {
        return false;
    }
    /* The write and the volume apply come FIRST here; the sample is last. The
     * stored value is 1..5, exactly as MenuBtn_SetSfxVol1/5 write 1 and 5. */
    pOpt->nSoundVolume = nStep + 1;
    Pads_MakeOneHot(pPads->abSfx, nStep);
    if (pHooks != NULL && pHooks->pfnApplySfxVolume != NULL) {
        pHooks->pfnApplySfxVolume(pHooks->pUser);
    }
    Menu_Play(pHooks, PUFO_MENU_SAMPLE_POST);
    return true;
}

void Menu_EndTurn(const MenuHooks *pHooks,
                  void (*pfnBeginEnemyTurn)(void *pUser))
{
    Menu_Play(pHooks, PUFO_MENU_SAMPLE_PRE);
    if (pfnBeginEnemyTurn != NULL) {
        pfnBeginEnemyTurn(pHooks != NULL ? pHooks->pUser : NULL);
    }
}

void Menu_ResizeWindow(const MenuHooks *pHooks,
                       void (*pfnResizeWindow)(void *pUser))
{
    Menu_Play(pHooks, PUFO_MENU_SAMPLE_POST);
    if (pfnResizeWindow != NULL) {
        pfnResizeWindow(pHooks != NULL ? pHooks->pUser : NULL);
    }
}

bool Menu_ToggleMusicMute(PufoOptions *pOpt, u8 *pPad, const MenuHooks *pHooks)
{
    bool bNew;

    if (pOpt == NULL) {
        return false;
    }
    if (pOpt->bMusicOff != 0) {
        pOpt->bMusicOff = 0;
        bNew = false;
    } else {
        pOpt->bMusicOff = 1;
        bNew = true;
    }
    if (pHooks != NULL && pHooks->pfnApplyMusicVolume != NULL) {
        pHooks->pfnApplyMusicVolume(pHooks->pUser);
    }
    if (pPad != NULL) {
        *pPad = bNew ? 1 : 0;
    }
    return bNew;
}

bool Menu_ToggleSfxMute(PufoOptions *pOpt, u8 *pPad, const MenuHooks *pHooks)
{
    bool bNew;

    if (pOpt == NULL) {
        return false;
    }
    if (pOpt->bSoundOff != 0) {
        pOpt->bSoundOff = 0;
        bNew = false;
    } else {
        pOpt->bSoundOff = 1;
        bNew = true;
    }
    if (pHooks != NULL && pHooks->pfnApplySfxVolume != NULL) {
        pHooks->pfnApplySfxVolume(pHooks->pUser);
    }
    if (pPad != NULL) {
        *pPad = bNew ? 1 : 0;
    }
    return bNew;
}

/* Menu_ToggleForceNoDrag @ 0x19d94 (pBank widget 88's click handler). The pad is the
 * inverse of the new value. */
bool Menu_ToggleForceNoDrag(PufoOptions *pOpt, u8 *pPad)
{
    bool bWasOn;
    bool bNew;

    if (pOpt == NULL) {
        return false;
    }
    bWasOn = pOpt->bForceNoDrag != 0;
    bNew = !bWasOn;
    pOpt->bForceNoDrag = bNew ? 1 : 0;
    if (pPad != NULL) {
        /* v0 is 1 when the flag was ON (now cleared) and 0 when it was OFF. */
        *pPad = bWasOn ? 1 : 0;
    }
    return bNew;
}

/* Light one pad of a five-pad group, guarding the index. The original indexes
 * straight through `32 * value + offset` with no bounds test, so a volume of 0
 * would light the sfx-mute pad and a speed above 4 would write past the group. */
static void Pad_Lit(u8 *pGroup, i32 nIdx)
{
    if (nIdx >= 0 && nIdx < PUFO_MENU_STEPS) {
        pGroup[nIdx] = 1;
    }
}

void Menu_SyncOptionPads(MenuPads *pPads, const PufoOptions *pOpt,
                         u16 *pnUiScreen, const MenuHooks *pHooks)
{
    if (pPads == NULL || pOpt == NULL) {
        return;
    }
    Menu_Play(pHooks, PUFO_MENU_SAMPLE_POST);

    /* The original's do-loop clears byte +10 of all 29 widgets of the block at
     * unk_ACE50, i.e. every pad byte here, including the confirm pad of widget 75
     * that it never lights again. */
    memset(pPads, 0, sizeof(*pPads));

    Pad_Lit(pPads->abSpeed, pOpt->nGameSpeed);
    Pad_Lit(pPads->abGamma, pOpt->nGamma);
    Pad_Lit(pPads->abMusic, pOpt->nMusicVolume - 1);
    Pad_Lit(pPads->abSfx, pOpt->nSoundVolume - 1);

    /* The seven flag pads are written as `if (flag) pad = 1`, which after the
     * clear is the same as the normalised assignment below. */
    pPads->abPen[0] = pOpt->bScrollByPen   != 0 ? 1 : 0;
    pPads->abPen[1] = pOpt->bShowBorder    != 0 ? 1 : 0;
    pPads->abPen[2] = pOpt->bRunByDblClick != 0 ? 1 : 0;
    pPads->abPen[3] = pOpt->bTurnByPen     != 0 ? 1 : 0;
    pPads->abPen[4] = pOpt->bMenuByPen     != 0 ? 1 : 0;
    pPads->bMusicMute = pOpt->bMusicOff != 0 ? 1 : 0;
    pPads->bSfxMute   = pOpt->bSoundOff != 0 ? 1 : 0;

    /* The forcenodrag pad is the INVERSE, the same quirk Menu_ToggleForceNoDrag
     * writes: `if (flag == 0) unk_AD1DA = 1`. */
    pPads->bForceNoDrag = pOpt->bForceNoDrag == 0 ? 1 : 0;

    if (pnUiScreen != NULL) {
        *pnUiScreen = PUFO_MENU_SCREEN_OPTIONS;
    }
}
