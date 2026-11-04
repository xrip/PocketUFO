# Audio lifecycle connection review

Status: bounded static review; no confirmed source defect. No audio/device,
decoder, test, game or debugger execution occurred.

## Original-to-port connections

The [raw capture](audio-lifecycle-raw-2026-10-28.json) records ARM instruction
bytes and call sites. IDA names are navigation aids; the track values below
come from the immediate R0 writes before the music calls.

| Screen | Original sample/music calls | Track | Original shutdown | Port path |
|---|---|---|---|---|
| Titles | 0x38200 / 0x38208 | 3 | 0x38270 | Titles_Init / Titles_Shutdown |
| Main menu | 0x38D48 / 0x38D50 | 0 | 0x38D80 | GameUI_InitStartup / StartUi_Shutdown |
| Tactical mission | 0x3D270 / 0x3D278 | 1 | 0x3D570 | Tact_InitMission hooks / World_FreeTactical sound shutdown |
| Geoscape | 0x86CF4 / 0x86CFC | 2 | 0x87244 | Geoscape_InitUI / GeoscapeUi_FreeAll |
| End screen | 0x38920 / 0x38988 | success4, failure3 | 0x389FC | EndScreen_Init / EndScreen_Shutdown |

The shared original routines are sample init `0x9E268`, music load `0x9E130`
and shutdown `0x9E3D4`. Shutdown frees the sample table entries, conditionally
frees music, closes the audio backend, then clears ready/music bytes. The port
reaches `Sound_Shutdown` through the matching direct calls or the App shutdown
hook. This establishes the listed callee bindings, not every transition caller.

## Live backend and memory ownership

`pufo_app.c` selects `g_pufoAudioWinmm` and binds both live option mute bytes
before the startup sample load. World sound callbacks reach `Sound_Play`.
Tactical init hooks reach the shared sample and music loaders.

`Winmm_SampleLoad` allocates owned PCM and copies/converts the WAV samples before
the shared loader frees its archive buffer. `Winmm_MusicLoad` creates an
openmpt module before that free. The linked package's
`build/_deps/pufo_openmpt_package-src/inc/libopenmpt/libopenmpt.h`, lines78-80,
states that create inputs are no longer needed after module creation.
Music replacement changes the shared module under the mixer lock and destroys
the old module afterward. Sample free clears voice references under the same
lock before freeing PCM. These are source ownership checks, not playback tests.

## Differences and open checks

- The port guards null handles, zeros freed table handles/count and clears the
  module pointer; the captured original shutdown does not perform those stores.
  These are explicit lifecycle adaptations, not original instructions.
- The WinMM/libopenmpt backend replaces FMOD. Audible mixing, loop behavior,
  resampling, device failures and focus pause/resume need runtime evidence.
- Sample/track availability was inventoried separately; no asset decode or
  new shipped-data execution was part of this check.
- Source calls and helper assertions cannot establish audible output or a
  complete new-game/mission/return campaign flow.
