/* pufo_menu.h - the options-menu button handlers.
 *
 * Evidence: MenuBtn_SetSpeed0..4 @ 0x18ecc/0x18f20/0x18f74/0x18fc8/0x1901c,
 *           MenuBtn_SetGamma0..4 @ 0x19070/0x190d0/0x19130/0x19190/0x191f0,
 *           Options_SetMusicVol1..5 @ 0x19a10/0x19a64/0x19ab8/0x19b0c/0x19b60,
 *           MenuBtn_SetSfxVol1..5 @ 0x19bb4/0x19c14/0x19c74/0x19cd4/0x19d34,
 *           MenuBtn_EndTurn @ 0x16494, MenuBtn_ResizeWindow @ 0x19930,
 *           Options_SyncUiFromMap_GotoScreen12 @ 0x188e4,
 *           Menu_OpenLoadSlots @ 0x18864, Menu_OpenSaveSlots @ 0x188a4,
 *           Menu_ConfirmQuitDialog @ 0x18e2c, Menu_CancelQuitDialog @ 0x18e04,
 *           App_QuitFromTact (0x18e18) is NOT ported: it is Tact_Shutdown(1) + App_QuitDisplay,
 *           which is app-shell work and waits on Tact_Shutdown (0x3d568) to be ported.
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
 * rather than duplicating the fields. The button handlers that need the modal or
 * the input handlers live in `pufo_menubtn.h` instead, because `pufo_game` must
 * not depend on `pufo_ui`.
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
 * ## The volume values are 1..5, not 0..4
 *
 * `Options_SetMusicVol1 @ 0x19A10` writes the immediate 1 into the music volume
 * dword at Map+0x150E0 and `Options_SetMusicVol5 @ 0x19B60` writes 5; the sfx
 * group is the same at Map+0x150DC (`MenuBtn_SetSfxVol1 @ 0x19BB4` writes 1,
 * `MenuBtn_SetSfxVol5 @ 0x19D34` writes 5). The pad lit is the FIRST of the five,
 * so the button number is the stored value and the pad index is value - 1. That
 * is also why `Options_SyncUiFromMap_GotoScreen12` reaches the music pads through
 * `32 * musicvolume + 554`, i.e. widget 77 + volume. The port keeps the buttons
 * 0-based and stores `nStep + 1`, which is what the registry round-trip and
 * `Options_MusicLevel` already assume.
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

/* The screen the options button opens (nUiScreen, Map+0x3A). */
#define PUFO_MENU_SCREEN_OPTIONS 12u

/* The sample the speed/gamma groups play before acting, and the one the sfx group
 * plays after. */
#define PUFO_MENU_SAMPLE_PRE  17
#define PUFO_MENU_SAMPLE_POST 16

/* The options screen (nUiScreen 12) is the 29-widget block pBank widgets 60..88.
 * Each widget's nState byte (+0x0A) is one byte of this struct, in widget order:
 *
 *   60..64 speed   0xACE5A   65..69 gamma    0xACEFA
 *   70..74 pen     0xACF9A   75    confirm   0xAD03A
 *   76    music mute 0xAD05A 77    sfx mute  0xAD07A
 *   78..82 music vol 0xAD09A 83..87 sfx vol  0xAD13A
 *   88    forcenodrag 0xAD1DA
 *
 * `Options_SyncUiFromMap_GotoScreen12 @ 0x188e4`, ported as Menu_SyncOptionPads, re-derives all 29 bytes from the
 * option block, which is why they live in one struct. */
typedef struct MenuPads {
    u8 abSpeed[PUFO_MENU_STEPS];   /* 0xACE5A, stride 0x20 */
    u8 abGamma[PUFO_MENU_STEPS];   /* 0xACEFA */
    u8 abPen[PUFO_MENU_STEPS];     /* 0xACF9A, scrollbypen..menubypen */
    u8 bConfirm;                   /* 0xAD03A, widget 75, never lit by the sync */
    u8 bMusicMute;                 /* 0xAD05A */
    u8 bSfxMute;                   /* 0xAD07A */
    u8 abMusic[PUFO_MENU_STEPS];   /* 0xAD09A */
    u8 abSfx[PUFO_MENU_STEPS];     /* 0xAD13A */
    u8 bForceNoDrag;               /* 0xAD1DA, the INVERSE of the option byte */
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
    /* Tact_LoadPalettes (0x377ec), NOT ported; it rebuilds the gamma LUTs. */
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
 * sample, unlike every other group. nStep is the 0-based pad index; the stored
 * value is nStep + 1, matching the original's 1..5. */
bool Menu_SetMusicVolume(PufoOptions *pOpt, MenuPads *pPads, i32 nStep,
                         const MenuHooks *pHooks);

/* MenuBtn_SetSfxVol1..5. Writes the sound volume, applies it, then plays sample
 * 16 -- i.e. the sample comes last here, not first. nStep is the 0-based pad
 * index; the stored value is nStep + 1. */
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

/* Options_SyncUiFromMap_GotoScreen12 @ 0x188e4. The options button does not change
 * any option: it re-reads the whole option block into the 29 pad bytes and opens
 * screen 12. Order: sample 16, clear all 29 pads, then
 *   abSpeed[nGameSpeed]        32 * nGameSpeed + 10,      widgets 60..64
 *   abGamma[nGamma]            32 * gamma + 170,          widgets 65..69
 *   abMusic[musicvolume - 1]   32 * musicvolume + 554,    widgets 78..82
 *   abSfx[soundvolume - 1]     32 * soundvolume + 714,    widgets 83..87
 * then the seven flag pads (the five pen flags, the two mute flags) are set only
 * when the flag is set, and the forcenodrag pad is set only when the flag is CLEAR.
 * Widget 75 (the confirm pad) is cleared and never re-lit. Finally nUiScreen = 12.
 *
 * Deviation: the original writes the four indexed pads unguarded, so a volume of 0
 * lights the SFX MUTE pad (widget 77) and a speed above 4 writes past the group.
 * The port only writes inside the group. */
void Menu_SyncOptionPads(MenuPads *pPads, const PufoOptions *pOpt,
                         u16 *pnUiScreen, const MenuHooks *pHooks);

#endif /* PUFO_MENU_H */
