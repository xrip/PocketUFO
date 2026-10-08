/* pufo_options.h - the game's option block and its registry persistence.
 *
 * Evidence: Options_SaveToRegistry @ 0x114d8 (writes every field below with its
 * registry value name), Reg_SetString @ 0x9ed0c, Reg_GetString @ 0x9edbc,
 * Sound_ApplyMusicVolume @ 0x9e454, Sound_ApplySfxVolume @ 0x9e4dc.
 *
 * The original keeps these inside the big `Map` context (pCtxBase @ 0x1139b8)
 * as raw byte/word spans that IDA names anActionPhaseSave0..3 and gap_86272.
 * The byte indices used by the code map to these absolute addresses:
 *
 *   0x150d5  +93   scrollbypen      (byte)
 *   0x150d6  +94   showborder       (byte)
 *   0x150d7  +95   runbydblclick    (byte)
 *   0x150d8  +96   turnbypen        (byte)
 *   0x150d9  +97   menubypen        (byte)
 *   0x150da  +98   musicoff         (byte)
 *   0x150db  +99   soundoff         (byte)
 *   0x150dc  +100  soundvolume      (dword)
 *   0x150e0  +104  musicvolume      (dword)
 *   0x150e4  +108  forcenodrag      (byte)
 *   0x150fc        nGameSpeed       (dword)
 *   0x15100        gamma            (dword)
 *
 * Registry key: HKEY_CURRENT_USER\Software\PocketUFO (SubKey @ 0xac680).
 * Values are REG_SZ even for the numbers (Reg_SetString writes type 1).
 */
#ifndef PUFO_OPTIONS_H
#define PUFO_OPTIONS_H

#include "pufo_common.h"

/* Volume is a 0..5 step index, not a raw level: Sound_Apply*Volume maps it
 * through 12*step+4 and then 4*value-1 to reach 0..255. */
#define PUFO_VOL_STEP_MAX 5

typedef struct PufoOptions {
    u8  bScrollByPen;    /* +93 */
    u8  bShowBorder;     /* +94 */
    u8  bRunByDblClick;  /* +95 */
    u8  bTurnByPen;      /* +96 */
    u8  bMenuByPen;      /* +97 */
    u8  bMusicOff;       /* +98 */
    u8  bSoundOff;       /* +99 */
    i32 nSoundVolume;    /* +100, 0..5 */
    i32 nMusicVolume;    /* +104, 0..5 */
    u8  bForceNoDrag;    /* +108 */
    i32 nGameSpeed;      /* 0x150fc */
    i32 nGamma;          /* 0x15100 */
} PufoOptions;

void Options_InitDefaults(PufoOptions *pOpt);

/* Persist / restore through HKCU\Software\PocketUFO. Missing values fall back to
 * the defaults, matching Reg_GetString's pDefault behaviour. */
void Options_SaveToRegistry(const PufoOptions *pOpt);
void Options_LoadFromRegistry(PufoOptions *pOpt);

/* Volume step -> the master level the audio backend receives.
 * Verified against Sound_ApplyMusicVolume @ 0x9e454 and Sound_ApplySfxVolume
 * @ 0x9e4dc:
 *     v = muted ? 0 : 12*step + 4;   level = 4*v - 1;
 * Unmuted steps 0..5 therefore give 15, 63, 111, 159, 207, 255.
 * Muted gives -1, not 0. That is the original's arithmetic and it is kept
 * exactly: FMOD 3 clamps it. Returning 0 here would be a silent behaviour
 * change in the backend, not a fix in the game logic. */
i32 Options_MusicLevel(const PufoOptions *pOpt);
i32 Options_SfxLevel(const PufoOptions *pOpt);

#endif /* PUFO_OPTIONS_H */
