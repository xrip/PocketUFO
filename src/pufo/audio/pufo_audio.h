/* pufo_audio.h - sound and music, behind a backend seam.
 *
 * Evidence: Sound_Play @ 0x9e0c0, Sound_LoadAndPlayMusic @ 0x9e130,
 *           Sound_InitAndLoadSamples @ 0x9e268, Sound_Shutdown @ 0x9e3d4,
 *           Sound_ApplyMusicVolume @ 0x9e454, Sound_ApplySfxVolume @ 0x9e4dc.
 *
 * The original links fmodce.dll (FMOD 3 for WinCE, ARM). That DLL cannot be
 * re-linked on Win64, so this module keeps the game-side logic exact and puts
 * the FMOD calls behind PufoAudioBackend. The Win64 backend is a no-op stub that
 * reports "not initialised" the same way the original does when FSOUND_Init
 * fails, so the game's control flow is unchanged.
 *
 * Behaviour that is part of the game logic, not the backend, and is therefore
 * reproduced here rather than delegated:
 *   - sample naming: "<prefix><n>.wav", stopping at the first missing index
 *   - music naming: "back<n>.it"
 *   - the 10 x 100 ms FSOUND_Init retry loop
 *   - Sound_Play's toggle of byte_187318 and its 0xC8 scale call
 *   - the soundoff / musicoff gates
 */
#ifndef PUFO_AUDIO_H
#define PUFO_AUDIO_H

#include "pufo_common.h"

/* The original's paSoundSamples @ 0x187428 spans 808 bytes up to the next named
 * global, i.e. 202 slots. Loads stop at the first missing file, so this is only
 * an upper bound; 202 is the real capacity. */
#define PUFO_MAX_SAMPLES 202

/* FSOUND_Init arguments, from Sound_InitAndLoadSamples @ 0x9e268. */
#define PUFO_AUDIO_MIXRATE   22000
#define PUFO_AUDIO_CHANNELS  32
/* Init retry guard, from Sound_InitAndLoadSamples @ 0x9e27c-0x9e2b4:
 *     i = 0; goto init;
 *   loop: if (i >= 10) goto done;   <- this 10 is the guard
 *         Sleep(100); ++i;
 *   init: if (FSOUND_Init(...) == 0) goto loop;
 * The first attempt happens before the guard is ever tested, so the guard value
 * 10 yields 11 attempts (i = 0..10) and 10 sleeps. */
#define PUFO_AUDIO_INIT_GUARD     10
#define PUFO_AUDIO_INIT_SLEEP_MS  100

/* FSOUND_ALL, the channel argument FSOUND_SetPaused is called with everywhere in the image: R0 = 0xFFFFFFFD at
 * 0x9E548, 0xA0534 and 0xA0620. It is the channel, not the pause flag -- the flag is the second argument. */
#define PUFO_FSOUND_ALL  (-3)

/* Backend seam. Every function returns true on success. */
typedef struct PufoAudioBackend {
    /* FSOUND_Init(mixrate, maxchannels, flags) */
    bool (*pfnInit)(u32 nMixRate, i32 nMaxChannels, u32 nFlags);
    /* FSOUND_SetHWND(hwnd) */
    void (*pfnSetWindow)(void *hWnd);
    /* FSOUND_Sample_Load(index, data, mode, offset, length) with index -2 */
    void *(*pfnSampleLoad)(const void *pData, u32 cbLength, u32 nMode);
    /* FSOUND_Sample_SetMode(sample, mode) */
    void (*pfnSampleSetMode)(void *pSample, u32 nMode);
    /* FSOUND_Sample_Free(sample) */
    void (*pfnSampleFree)(void *pSample);
    /* FSOUND_PlaySound(channel, sample) with channel -1 */
    i32 (*pfnPlaySound)(void *pSample);
    /* FSOUND_SetSFXMasterVolume(volume) */
    void (*pfnSetSfxVolume)(i32 nVolume);
    /* FSOUND_SetPaused(nChannel, bPaused), two arguments, not one: the IDB carried the import as
     * `void __stdcall FSOUND_SetPaused(signed __int8 paused)` and Hex-Rays therefore showed the flag coming from R0,
     * which is the channel. The disassembly is `MOV R1, #1; MOV R0, #0xFFFFFFFD; BL FSOUND_SetPaused` at
     * 0xA052C..0xA0534 and `MOV R1, #0; ...` at 0xA0618..0xA0620. */
    void (*pfnSetSfxPaused)(i32 nChannel, i32 bPaused);
    /* FMUSIC_LoadSongEx(data, offset, length, mode, samplelist, count) */
    void *(*pfnMusicLoad)(const void *pData, u32 cbLength, u32 nMode);
    void (*pfnMusicSetLooping)(void *pMod, i32 bLoop);
    void (*pfnMusicOptimize)(void *pMod, i32 nMaxChannels, i32 nMinVolume);
    void (*pfnMusicPlay)(void *pMod);
    void (*pfnMusicSetVolume)(void *pMod, i32 nVolume);
    void (*pfnMusicSetPaused)(void *pMod, i32 bPaused);
    void (*pfnMusicFree)(void *pMod);
    /* FSOUND_Close() */
    void (*pfnClose)(void);
} PufoAudioBackend;

/* Install the backend. Pass NULL to fall back to the built-in no-op stub.
 * Must be called before Sound_InitAndLoadSamples. */
void Sound_SetBackend(const PufoAudioBackend *pBackend);

/* Hand the module the live mute cells. The original's Sound_Play reads the soundoff
 * byte out of the options block (pCtxBase + 99, PufoOptions.bSoundOff) before it
 * reaches FSOUND_PlaySound, and Sound_LoadAndPlayMusic pauses the module right after
 * FMUSIC_PlaySong when Map_bMusicOff @ 0x128A92 stands (test at 0x9E228, pause at
 * 0x9E230). With the pair installed these are read live; until then the module falls
 * back to the SetSfxMuted mirror and the load-time pause cannot happen. */
void Sound_SetMuteBytes(const u8 *pbSoundOff, const u8 *pbMusicOff);

/* Loads "<pszPrefix><n>.wav" for n = 0, 1, ... until one is missing.
 * Retries FSOUND_Init 11 times with a 100 ms sleep between tries. The count is
 * 11, not 10: the original jumps straight into the first FSOUND_Init at i = 0,
 * then its guard is `i < 10`, so attempts happen at i = 0..10. Verified from the
 * disassembly @ 0x9e27c-0x9e2b4. Returns true if the ready flag was set. */
bool Sound_InitAndLoadSamples(const char *pszPrefix);

/* Releases every loaded sample, the music module, and closes the backend. */
void Sound_Shutdown(void);

/* Mirrors the original's soundoff byte, which Sound_Play reads from the game
 * context (+99). The options screen calls this when the SFX mute is toggled.
 * Kept as a seam rather than a parameter so Sound_Play keeps the original's
 * one-argument shape. */
void Sound_SetSfxMuted(bool bMuted);

/* Plays sample index nSample. No-op when audio is not initialised or muted.
 * Note the ordering, which matters: the SFX toggle and the 0xC8 RNG draw happen
 * BEFORE the mute test in the original (Sound_Play @ 0x9e0c0), so the global RNG
 * stream advances even while muted. */
void Sound_Play(i32 nSample);

/* Loads "back<nTrack>.it", enables looping, optimises to 16 channels, plays,
 * and pauses immediately if music is muted. */
void Sound_LoadAndPlayMusic(i32 nTrack);

/* Applies the option block's volume levels. */
void Sound_ApplyMusicVolume(i32 nLevel, u8 bMuted);
void Sound_ApplySfxVolume(i32 nLevel, u8 bMuted);

/* The audio half of PocketUFO_WndProc's focus arms: FSOUND_SetPaused(FSOUND_ALL, 1) on WM_KILLFOCUS
 * (0xA052C..0xA0534) and (FSOUND_ALL, 0) on WM_SETFOCUS (0xA0618..0xA0620) -- unconditional, with no byte_187F24
 * audio-ready test, unlike Sound_ApplySfxVolume -- and then FMUSIC_SetPaused(module, 1/0) only when byte_187F28 says
 * a module is loaded (0xA0538 / 0xA0624). The module is the one this module owns, s_pMusicMod. */
void Sound_SetPaused(bool bPaused);

/* State, mirroring byte_187F24 (audio ready) and byte_187F28 (music playing). */
bool Sound_IsInitialised(void);
bool Sound_HasMusic(void);
i32  Sound_GetSampleCount(void);

#endif /* PUFO_AUDIO_H */
