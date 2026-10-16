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
 *
 * The loader side is Map_InitAndLoadRegistry @ 0x22648, the Map subsystem's startup
 * entry: the CRT init-term thunk at 0x22604 runs it and then Map_RegisterAtexit.
 * It clears the tactical scratch first and then reads the twelve values below.
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

/* Map_InitAndLoadRegistry @ 0x22648 -- the Map subsystem's startup entry, reached
 * from the CRT init-term table through the thunk at 0x22604 (this function, then
 * Map_RegisterAtexit). It initialises the Map block's scratch and then loads the
 * option block. The scratch lives in caller storage, the way TactInitEnv carries
 * the per-mission flags, because the port splits the original 152 KB block across
 * several models; every member cites the Map offset the original writes.
 *
 * The original writes every one of these unconditionally. The port writes through
 * the members the caller owns and skips the ones it passes as NULL -- the shell
 * runs no tactical mission, so it owns neither the battle-ended byte nor the
 * throw-target slot, and clearing a field of a block that does not exist has no
 * counterpart. Recorded in PLAN.md. */
typedef struct MapInitEnv {
    PufoOptions *pOptions;

    u8   *pbDeployPanelOpen; /* 0x15110 */
    u8   *pbMapPhase;        /* 0x0037 unk_1139EF. The original stores a byte; the
                              * port's home is the wider anim counter, and a zero
                              * store is the same value in either width. */
    u8   *pbBattleEnded;     /* 0x005C unk_113A14 */
    u8   *pbSlot2Byte0;      /* 0x0068 LOBYTE(apUiSlotsA[2]), the sweep gate */
    u8   *pbSlot8Byte0;      /* 0x0080 LOBYTE(apUiSlotsA[8]) */
    i32  *pnUiSlot3;         /* 0x006C */
    i32  *pnUiSlot4;         /* 0x0070, set to 200 */
    u32  *pnUiSlot5;         /* 0x0074: the original clears its four bytes one by one */
    i32  *pnUiSlot7;         /* 0x007C */
    i32  *pnUiSlot25;        /* 0x00C4 */
    i32  *pnUiSlot26;        /* 0x00C8, set to Game_RandU16(3) */
    i32  *pnPhaseSave112;    /* 0x150E8 anActionPhaseSave0[112] */
    void **ppThrowTarget;    /* 0x150F0 pThrowTargetUnit */
} MapInitEnv;

void Map_InitAndLoadRegistry(const MapInitEnv *pEnv);

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
