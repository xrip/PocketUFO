/* pufo_menu.h - the options-menu button handlers.
 *
 * Evidence: MenuBtn_SetSpeed0..4 @ 0x18ecc/0x18f20/0x18f74/0x18fc8/0x1901c,
 *           MenuBtn_SetGamma0..4 @ 0x19070/0x190d0/0x19130/0x19190/0x191f0,
 *           Options_SetMusicVol1..5 @ 0x19a10/0x19a64/0x19ab8/0x19b0c/0x19b60,
 *           MenuBtn_SetSfxVol1..5 @ 0x19bb4/0x19c14/0x19c74/0x19cd4/0x19d34,
 *           MenuBtn_EndTurn @ 0x16494, MenuBtn_ResizeWindow @ 0x19930.
 *
 * ## These write the SAME option bytes the options module owns
 *
 * Every one of the twenty option buttons writes a value into the context that
 * `pufo_options.h` already documents by name:
 *
 *     nGameSpeed   @ 0x150fc   <- MenuBtn_SetSpeed0..4
 *     gamma        @ 0x15100   <- MenuBtn_SetGamma0..4  (a dword write to gap_86272)
 *     musicvolume  @ 0x150e0   <- Options_SetMusicVol1..5
 *     soundvolume  @ 0x150dc   <- MenuBtn_SetSfxVol1..5
 *
 * (`anActionPhaseSave0[104]` is 0x15078 + 104 = 0x150E0, and `[100]` is 0x150DC,
 * which is exactly how the original reaches them.) So the menu layer drives the
 * same option block `pufo_options` persists, and the port passes that struct in
 * rather than duplicating the fields.
 *
 * ## The one-hot pad arrays
 *
 * Each button also lights exactly one of five pad-indicator bytes. The five
 * arrays sit at a 0x20 stride and are otherwise identical in shape:
 *
 *     speed pads 0xACE5A, gamma 0xACEFA, music 0xAD09A, sfx 0xAD13A
 *
 * Every handler clears all five pads of its group and sets one, so the arrays are
 * one-hot at all times.
 *
 * ## Per-group differences that are real
 *
 *   speed : sample 17, pads, then GameSpeed_Set (which kills/restarts timers)
 *   gamma : sample 17, pads, write gamma, then Tact_LoadPalettes to rebuild them
 *   music : NO sample at all; write the volume, then Sound_ApplyMusicVolume
 *   sfx   : write the volume, then Sound_ApplySfxVolume, THEN sample 16
 *
 * The music group is the only one that plays no sample, and the sfx group is the
 * only one that plays after applying the volume rather than before. Both are
 * reproduced. Note also that the speed and gamma groups index their pads from 0
 * while the volume groups' functions are named 1..5 -- the naming is the
 * original's, and the pad index is 0-based in every group.
 */
#ifndef PUFO_MENU_H
#define PUFO_MENU_H

#include "game/pufo_options.h"
#include "game/pufo_tactctl.h"
#include "pufo_common.h"

/* Each group has five selectable steps. */
#define PUFO_MENU_STEPS 5

/* The sample the speed/gamma groups play before acting, and the one the sfx group
 * plays after. */
#define PUFO_MENU_SAMPLE_PRE  17
#define PUFO_MENU_SAMPLE_POST 16

/* The four one-hot pad groups. */
typedef struct MenuPads {
    u8 abSpeed[PUFO_MENU_STEPS];   /* 0xACE5A, stride 0x20 */
    u8 abGamma[PUFO_MENU_STEPS];   /* 0xACEFA */
    u8 abMusic[PUFO_MENU_STEPS];   /* 0xAD09A */
    u8 abSfx[PUFO_MENU_STEPS];     /* 0xAD13A */
} MenuPads;

/* What the handlers need from the world. Every effect is injected because each
 * belongs to another subsystem: the palettes to the palette loader, the volume
 * to the audio backend, the timer restart to GameSpeed_Set (already ported). */
typedef struct MenuHooks {
    void (*pfnPlaySample)(i32 nSample, void *pUser);
    /* Sound_ApplyMusicVolume @ 0x9e454 / Sound_ApplySfxVolume @ 0x9e4dc. Both read
     * the option block themselves in the original; the port passes the block back
     * through pUser's owner, so these take no argument. */
    void (*pfnApplyMusicVolume)(void *pUser);
    void (*pfnApplySfxVolume)(void *pUser);
    /* Tact_LoadPalettes @ 0x377ec, which rebuilds the gamma LUTs. */
    void (*pfnLoadPalettes)(void *pUser);
    void *pUser;
} MenuHooks;

/* MenuBtn_SetSpeed0..4. Plays sample 17, makes the speed pads one-hot, then calls
 * GameSpeed_Set on pClock.
 *
 * Note the original passes the raw constant (0..4) to GameSpeed_Set, so this
 * inherits GameSpeed_Set's documented behaviour: speeds above 4 store the value
 * but leave the timers alone. Only 0..4 have buttons, so that branch is
 * unreachable from the UI. Returns false for an out-of-range step. */
bool Menu_SetSpeed(PufoOptions *pOpt, GameClock *pClock, MenuPads *pPads,
                   i32 nStep, const MenuHooks *pHooks);

/* MenuBtn_SetGamma0..4. Plays sample 17, makes the gamma pads one-hot, writes the
 * gamma dword, then calls Tact_LoadPalettes. */
bool Menu_SetGamma(PufoOptions *pOpt, MenuPads *pPads, i32 nStep,
                   const MenuHooks *pHooks);

/* Options_SetMusicVol1..5. Writes the music volume, then applies it. Plays NO
 * sample, unlike every other group. */
bool Menu_SetMusicVolume(PufoOptions *pOpt, MenuPads *pPads, i32 nStep,
                         const MenuHooks *pHooks);

/* MenuBtn_SetSfxVol1..5. Writes the sound volume, applies it, then plays sample
 * 16 -- i.e. the sample comes last here, not first. */
bool Menu_SetSfxVolume(PufoOptions *pOpt, MenuPads *pPads, i32 nStep,
                       const MenuHooks *pHooks);

/* MenuBtn_EndTurn @ 0x16494: sample 17, then Tact_BeginEnemyTurn. Injected
 * because the turn system needs the whole world. */
void Menu_EndTurn(const MenuHooks *pHooks, void (*pfnBeginEnemyTurn)(void *pUser));

/* MenuBtn_ResizeWindow @ 0x19930: sample 16, then App_ResizeMainWindow. */
void Menu_ResizeWindow(const MenuHooks *pHooks, void (*pfnResizeWindow)(void *pUser));

/* Ui_ToggleMusicMute @ 0x19948 / Ui_ToggleSfxMute @ 0x199ac. Flips bMusicOff /
 * bSoundOff, applies the matching volume and mirrors the new flag into its
 * one-byte pad indicator (byte_AD05A / byte_AD07A, passed as pPad, may be NULL).
 * Returns the new flag value. The original offers these as separate functions;
 * the port keys the behaviour on the option byte to stay DRY. */
bool Menu_ToggleMusicMute(PufoOptions *pOpt, u8 *pPad, const MenuHooks *pHooks);
bool Menu_ToggleSfxMute(PufoOptions *pOpt, u8 *pPad, const MenuHooks *pHooks);

/* TactHud_ToggleFlag128A9C @ 0x19d94. Toggles the `forcenodrag` option byte
 * (+108) and writes its one-byte pad indicator. Note the pad is the INVERSE of the
 * new flag value: turning the flag ON writes 0, turning it OFF writes 1 -- unlike
 * the mute toggles. Returns the new flag value. */
bool Menu_ToggleForceNoDrag(PufoOptions *pOpt, u8 *pPad);

#endif /* PUFO_MENU_H */
