/* pufo_audio_winmm.h - the Win64 output backend at the PufoAudioBackend seam.
 *
 * Install it with Sound_SetBackend before Sound_InitAndLoadSamples; passing NULL
 * back returns the module to its no-op stub, which is what keeps every control-flow
 * test (init retry, sample naming, ready flag) independent of a sound device.
 *
 * Evidence for the seam itself is in pufo_audio.h: FSOUND_Init(22000, 32, 0) at
 * 0x9E2A8, FSOUND_SetHWND at 0x9E2C0, FSOUND_Sample_Load(-2, data, 0xA000)
 * at 0x9E38C, FSOUND_Sample_SetMode(1) at 0x9E398, FMUSIC_LoadSongEx(data, 0, len,
 * 0x8000) at 0x9E1F4, FMUSIC_SetLooping/OptimizeChannels(16, 10)/PlaySong at
 * 0x9E1F8..0x9E210, FMUSIC_SetPaused at 0x9E230, FSOUND_SetSFXMasterVolume at
 * 0x9E518, FSOUND_SetPaused(FSOUND_ALL, flag) at 0x9E548 / 0xA0534 / 0xA0620.
 *
 * Deviations from fmodce.dll, all recorded in docs/parity/render-audio.md:
 *  - The mono WinMM stream runs at the shipped WAVs' native 11025 Hz, not at
 *    FSOUND_Init's 22000 mixrate: the SFX play 1:1 with no resampling
 *    (user-directed, 2026-11-04). All 35 shipped TACT*.WAV are 11025 Hz /
 *    8-bit / mono; 8-bit data is widened to 16-bit, voices read frames 1:1, and
 *    OpenMPT renders the shipped Impulse Tracker music at the same 11025 Hz.
 *  - The output queue is 8 x 256 frames (~186 ms), a Win64 sizing choice.
 *  - FSOUND_SetHWND has no winmm counterpart: waveOut needs no window.
 *  - OpenMPT handles music playback, but FMUSIC_OptimizeChannels has no direct
 *    equivalent here. The music and SFX share one 22000 Hz mono output queue.
 *  - SFX use 32 software voices, so repeated sounds may overlap. FMOD's exact
 *    channel allocation and mixing behavior are not reproduced.
 */
#ifndef PUFO_AUDIO_WINMM_H
#define PUFO_AUDIO_WINMM_H

#include "audio/pufo_audio.h"

/* The backend table to hand Sound_SetBackend. */
extern const PufoAudioBackend g_pufoAudioWinmm;

/* True when the machine has at least one wave-output device. */
bool PufoAudioWinmm_HasDevice(void);

/* The active waveOut format rate (the native 11025 Hz), or 0 while the device
 * is closed. Test helper only. */
u32 PufoAudioWinmm_GetOutputRate(void);

/* Copy mono PCM through the same per-voice frame path the mixer uses. Test helper only. */
u32 PufoAudioWinmm_TestCopyNativeFrames(const i16 *pSource, u32 nSourceFrames,
                                        i16 *pOutput, u32 nOutputCapacity);

/* Waits up to nTimeoutMs for an SFX voice's final mixed buffer to finish.
 * Returns true when that completion arrives. Test helper only. */
bool PufoAudioWinmm_WaitDone(u32 nTimeoutMs);

/* Drops up to nMaxSignals already-queued SFX completion signals. Test helper only. */
void PufoAudioWinmm_DrainDone(i32 nMaxSignals);

/* Waits for a nonzero music mix buffer to complete through waveOut. Test helper only. */
bool PufoAudioWinmm_WaitMusicDone(u32 nTimeoutMs);

#endif /* PUFO_AUDIO_WINMM_H */
