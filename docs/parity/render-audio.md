# Parity evidence — rendering and audio

Task: recreate rendering (software surface, blitter, palette) and audio.
Tests: `tests/test_render.c`, `tests/test_audio.c`.

## Files created and the functions they answer to

| Created | Original evidence |
|---|---|
| `render/pufo_render.{h,c}` | `SoftSurf_ctor` @ `0x9f96c`, `dtor` @ `0x9f97c`, `AllocPixels` @ `0x9f9a4`, `GetWidth` @ `0x9f994`, `GetHeight` @ `0x9f99c`, `GetPixels` @ `0x9fcb4`, `Render_SetTarget` @ `0x9faa4`, `Render_BlitSoft_LoRes` @ `0x9fa7c`, `HiRes` @ `0x9fa90`, `Render_Frame` @ `0x22310`, `Blit_LoadSurface` @ `0xa3964`, `Blit_SetTargetParams` @ `0xa39ac`, `Blit_CopyRows` @ `0xa3a14`, `Blit_CopyRows_HiRes` @ `0xa3bb8`, `BlitParams_Set` @ `0xa47b0`, `Blit_SetSrcRect` @ `0xa3f5c`, `Store_dword_A3028` @ `0xa3624`, `SoftSurf_Blit_Inner` @ `0xa47d0`, `BlitFlip_Inner` @ `0xa49a0`, `BlitIndexed_Inner` @ `0xa306c`, `BlitIndexedKeyed_Inner` @ `0xa3638`, `FillRect_Inner` @ `0xa3f94`, `BlitIndexed` @ `0x9e9f4`, `BlitIndexedKeyed` @ `0x9ea34`, `Blit` @ `0x9e558`, `BlitFlip` @ `0x9e590`, `BlitSrcRect` @ `0x9ebf8`, `BlitRect` @ `0x9ebb0`, `BlitCopyRaw` @ `0xa3db8`, `PadImage_ctor` @ `0x11000`, `Load` @ `0x11020`, `Blit` @ `0x110dc`, `Palette_Load` @ `0x3e47c` |
| `audio/pufo_audio.{h,c}` | `Sound_Play` @ `0x9e0c0`, `Sound_LoadAndPlayMusic` @ `0x9e130`, `Sound_InitAndLoadSamples` @ `0x9e268`, `Sound_Shutdown` @ `0x9e3d4`, `Sound_ApplyMusicVolume` @ `0x9e454`, `Sound_ApplySfxVolume` @ `0x9e4dc` |
| `game/pufo_options.{h,c}` | `Options_SaveToRegistry` @ `0x114d8` (all twelve keys), volume mapping from `Sound_Apply*Volume` |
| `platform/pufo_reg.{h,c}` | `Reg_SetString` @ `0x9ed0c`, `Reg_GetString` @ `0x9edbc` |
| `util/pufo_str.{h,c}` | `_strupr` @ `0xa60e4` + the CRT copies, factored out of `dat`/`asset`/`render` |

## Structures recovered from the IDB

| Type | Size | Layout source |
|---|---|---|
| `SoftSurf` | 40 | `SoftSurf_AllocPixels` @ `0x9f9a4` |
| `BlitState` | 40 | `Blit_LoadSurface` + `Blit_SetTargetParams` |
| `RenderTarget` | 24 | `Render_SetTarget` @ `0x9faa4` |
| `PadImage` | 20 | `PadImage_ctor` @ `0x11000`, `PadImage_Load` @ `0x11020` |

## Option defaults: corrected against the original

`Options_InitDefaults` previously invented its own defaults (sound 3, music 3,
gamespeed 1) and `Options_LoadFromRegistry` returned out-of-range registry values
unchanged. `Map_InitAndLoadRegistry` @ 0x22648 shows the real contract.

Each setting is read with `Reg_GetString(name, <literal default>)`, then the numeric
ones are range checked with a **fixed fallback constant**, not the caller's default:

| key | default | valid range | fallback |
|---|---|---|---|
| `gamespeed` | `"4"` | 0..4 | 4 |
| `gamma` | `"2"` | 0..4 | 2 |
| `soundvolume` | `"5"` | 1..5 | 5 |
| `musicvolume` | `"5"` | 1..5 | 5 |

Flag values are read as `strcmp(value, "0") != 0`, so the literal default string
decides the default: `"1"` means set and `"0"` means clear. That gives
`scrollbypen`, `showborder`, `runbydblclick` and `menubypen` set by default, and
`turnbypen`, `musicoff`, `soundoff` and `forcenodrag` clear.

Two details worth keeping:

- The volume range **excludes 0**, so a stored `0` is out of range and is promoted
  to 5. Reading it as a valid "silent" step would be a behaviour change.
- `Options_LoadIntClamped` spells the fallback out separately from the default even
  though they are equal today; they are different concepts in the original.

Deviations: the port clamps in C rather than through the original's `atoi` on a
stack buffer copy, which is the same arithmetic for numeric input. The original's
`strcmp`-based flag read is preserved, so non-numeric text still sets a flag.

## Bugs found and fixed during this task

Two real defects, both caught by the tests rather than by inspection.

1. **Missing row term in every blitter.** Each glyph row computed its destination
   as `pSrc + stride * y + x` but never added the loop's row index `y2`, so every
   source row overwrote the same destination row. The surface-address arithmetic
   came straight from the disassembly, but the loop variable was dropped when
   transcribing it. Fixed in all five blitters (`Blit_Indexed` all three pixel
   modes, `Blit_Blend`, `Blit_FlipBlend`, `SoftSurf_FillRectEx`). Symptom was
   "top-left pixel got the bottom row's value".

2. **Init retry loop off by one.** A plain `for (…; !init && i < 10; …)`
   performs 12 attempts. The original jumps *into* the first `FSOUND_Init` before
   testing its guard, so `i < 10` yields 11 attempts and 10 sleeps. Rewritten to
   the original's attempt-then-check-then-sleep shape.

A third check also corrected a wrong assumption of mine, not of the code:
`byte_187F24` is set on **both** the success and the give-up path of
`Sound_InitAndLoadSamples` @ `0x9e268`, so the game proceeds as if audio is ready
even when the backend never initialised. That is asserted explicitly in
`test_audio.c` so nobody "fixes" it later.

## Verified behaviours

### Rendering

| Behaviour | Evidence | Test check |
|---|---|---|
| Surface carries a 4-byte header; `pPixels == pAlloc + 4` | `SoftSurf_AllocPixels` @ `0x9f9a4` allocates `W*H*bytesPP + 4` and returns `pAlloc + 4` | `pPixels == pAlloc + 4`, header holds `w=240, h=320` |
| Pixel mode outside 0..3 leaves `nBytesPP` 0 | same function: no `else` for the byte width | `nBytesPP == 0` for mode 9 |
| Palette conversion per display flag | `Palette_Load` @ `0x3e47c` | three branches checked against real CLASS.PAL |
| Colour-key skips matching indices | `BlitIndexedKeyed_Inner` @ `0xa3638` | keyed pixels untouched, others drawn |
| Glyph clipping shifts source and clamps to surface | `BlitIndexed_Inner` @ `0xa306c` preamble | left/top/right clips and off-surface no-op |
| FillRect clips instead of overrunning | `FillRect_Inner` @ `0xa3f94` | oversized fill covers exactly the surface |
| Row copy writes the logical surface to the target 1:1 | `Blit_CopyRows` @ `0xa3a14` | 12/12 pixels match |

Palette ground truth was extracted independently from `main.dat`:

| Check | Expected | Source |
|---|---|---|
| CLASS.PAL entry 0 RGB565 | `0x0040` | file bytes rgb(4,8,7) |
| CLASS.PAL entry 1 RGB565 | `0x32E9` | file bytes rgb(48,92,79) |
| CLASS.PAL entry 255 RGB565 | `0xFFFF` | file bytes rgb(255,255,255) |
| CLASS.PAL entry 0 RGB555 | `0x0020` | same triple, 5-5-5 packing |
| CLASS.PAL entry 0 BGR888 | `0x070804` | `b | g<<8 | r<<16` |

### Audio

| Behaviour | Evidence | Test check |
|---|---|---|
| Samples load as `"tact" + n + ".wav"`, stopping at the first gap | `Sound_InitAndLoadSamples` @ `0x9e268`; prefix `"tact"` confirmed at the `GameUI_InitStartup` call site @ `0x38d38` | 35 samples, matching main.dat `TACT0..TACT34.WAV` with no gaps |
| Sample load mode `0xA000`, then `SetMode(1)` | `FSOUND_Sample_Load`/`SetMode` call args | both asserted |
| Music loads as `"back" + n + ".it"`, mode `0x8000` | `Sound_LoadAndPlayMusic` @ `0x9e130` | mode `0x8000`, loop on, optimize 16 channels, played |
| Music volume step → level `4*(12*step+4)-1` | `Sound_ApplyMusicVolume` @ `0x9e454` | steps 0,3,5 → 15,159,255 |
| Muted volume is `-1`, not 0 | same function: `v = muted ? 0 : 12*step+4` then `4*v-1` | muted → `-1` |
| SFX volume also sends the unpause-all sentinel `-3` | `Sound_ApplySfxVolume` @ `0x9e4dc` | `-3` observed |
| `Sound_Play` advances the RNG even when muted | `Sound_Play` @ `0x9e0c0` draws `Game_RandU16(0xC8)` before the soundoff test | noted in code; mute path exercised |
| Registry: HKCU `Software\PocketUFO`, REG_SZ, 0xFF length cap | `Reg_SetString` @ `0x9ed0c`, `Reg_GetString` @ `0x9edbc` | same key, same type, same cap |

## Deviations, each with its reason

| Deviation | Reason |
|---|---|
| FMOD is behind `PufoAudioBackend`; the shipped backend is a no-op stub | `fmodce.dll` is an ARM/WinCE FMOD 3 build and cannot be linked on Win64. The stub returns failure exactly as a missing DLL would, so the game's control flow is unchanged |
| `Render_BlitSurface` always uses the 1:1 row copy; `Blit_CopyRows_HiRes` is not ported | HiRes wrote each source pixel as a 2x2 block to fill a 480x640 panel. The Win64 target presents the 240x320 surface at integer scale instead, so the HiRes path has no equivalent target |
| GAPI display modes and per-mode pitch sign flips dropped | Those compensated for rotated Pocket PC panels. A top-down DIB needs none. Already recorded in `docs/parity/scaffold.md` |
| `Sound_Play` bounds-checks the sample index | the original indexes `paSoundSamples` unchecked, which is a buffer overrun for a bad index. The port guards it because the original behaviour there is undefined, not defined. The only place the port is deliberately stricter |
| Original NULL sample handle is still passed to the backend | the original has no NULL guard; removing it would change behaviour on a partially-loaded sample table |
| `Blit_SetSrcRect` drops the unused `x`/`y` parameters | the original stores four globals but only the width/height pair is read by `FillRect_Inner`. Dropping the dead pair keeps the signature honest; noted because the signature visibly differs from the IDB |
| `pufo_str*` helpers instead of `strcpy`/`strcat`/`_strupr` | identical behaviour; the MSVC CRT marks the originals deprecated and `/WX` rejects them |

## Naming corrections applied

- `SoftSurf_BlitFlipEx` @ `0x9fb80`: IDA's parameter names (`nW`, `nH`, `nKey`) are
  wrong. The disassembly shows the 5th argument is forwarded straight through as a
  **palette pointer** (no `aPalette[256*slot]` conversion, unlike every sibling),
  and the 6th is a **mask byte array**. The `RadarScreen_*` call sites @ `0x5ec30`
  pass a computed buffer pointer. Named `pPal`/`pMask`.
- Likewise `SoftSurf_BlitFlip` is not a mirror despite the name; it is the
  per-pixel masked blend, and it uses `256 - mask` for the destination weight
  while `Blit_Inner` uses `255 - alpha`. Both differences are reproduced.

## Checks run

| Check | Result |
|---|---|
| `cmake --build build --config Release` | 0 errors, 0 warnings (`/W4 /WX`) |
| `build/tests/Release/test_render.exe` | **PASS, 0 failures** (43 checks) |
| `build/tests/Release/test_audio.exe` | **PASS, 0 failures** (35 checks) |
| `build/tests/Release/test_data.exe` | **PASS, 0 failures** (data layer still green) |
| `build/src/pufo/Release/pocketufo.exe` | links |

## Residual uncertainty

- The blend paths (`Blit_Blend`, `Blit_FlipBlend`) are implemented from the
  disassembly but have **no test coverage**, because every 16bpp case needs a
  known-good reference image that does not exist here. The channel arithmetic is
  transcribed literally; the `255` vs `256` difference between the two functions
  is deliberate and copied, not a typo. This is the weakest area of this task.
- `Render_Frame` @ `0x22310` dispatches on `pCtxBase.nUiScreen` across ~18
  screens. That dispatcher is **not** ported here: the screens it calls belong to
  the `tact-ui`, `strat-ui`, `titles`, and `strat` tasks. Only the render
  primitives it drives are done.
- `SoftSurf_BlitRect @ 0x9ebb0` / `SoftSurf_BlitSrcRect @ 0x9ebf8` /
  `Blit_CopyToXY @ 0xa42e4` are ported (the two bound forms pin `g_pMainSurface`).
  They gate on **different fields** and that is the original's own shape: the fill
  reads `blitState.nPixelMode`, the TARGET mode, published only by
  `Blit_SetTargetParams @ 0xa39ac` from `Render_SetTarget @ 0x9faa4`, which
  `GX_PresentFrame @ 0x9ea7c` arms on every present, while the blend reads
  `blitState.nSurf2`, the SURFACE mode. A caller that has a surface but no armed
  target therefore fills nothing and blends normally — proven in `test_titles`,
  which had exactly that shape and drew no scroll<50 fill (Part 39).
- `PadImage_Blit` @ `0x110dc` is ported as a shim over `SoftSurf_BlitIndexed`,
  which is what the original does. It cannot be exercised until the global
  surface is created by `Assets_LoadStartup` (the `boot-assets` work).
