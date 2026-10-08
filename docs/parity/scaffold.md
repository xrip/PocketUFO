# Parity evidence — scaffold

Task: scaffold the Win64 CMake + Clang project with a module layout matching the
IDB subsystem map.

Original: `pufobeta.exe` (WinCE/ARM32) in `pufobeta.exe.i64`.

## Files created and the functions they answer to

| Created | Original evidence | Match | Deviation |
|---|---|---|---|
| `CMakeLists.txt` | project-wide | n/a | toolchain is Clang/CMake per project rule, not the original CE build system |
| `pufo_common.h` | `InitInstance` @ `0x9fd48` | `DisplayMode` values 0..3 in mode order 240x320, 320x240, 480x640, 640x480 | none |
| `platform/pufo_gx.{h,c}` | `GX_PresentFrame` @ `0x9ea7c`, `InitInstance` @ `0x9fd48` | 240x320 16bpp RGB565 surface, pitch fields kept signed | see drops below |
| `util/pufo_ptrarray.{h,c}` | `PtrArray_ctor` @ `0xa0f80`, `_Insert` @ `0xa0f94`, `_Add` @ `0xa1044`, `_Remove` @ `0xa1054`, `_Clear` @ `0xa1120`, `_Get` @ `0xa1130`, `_GetCount` @ `0xa1154`, `_DeleteAll` @ `0x3e434`, `_DeleteAt` @ `0x82bac` | layout `{int nCount; void **apItems;}`, `0xFFFF` = all, realloc-per-change | none |
| `util/pufo_rand.{h,c}` | `Game_RandU16` @ `0x3e59c`, seeding @ `0x9fd48` | `1/32768` scale, truncate to u16 | modulo-32768 guard added; see below |
| `app/pufo_app.c` | `WinMain` @ `0x9fcc4`, `InitInstance` @ `0x9fd48`, `PocketUFO_WndProc` @ `0xa03ec`, `App_QuitDisplay` @ `0xa0388` | single instance, class name/style, WS_VISIBLE, accelerator loop | see drops below |

## Verified function details

### `Game_RandU16` @ `0x3e59c`

Decompiled:

```c
v2 = rand();
v3 = itos(v2); v4 = muls(v3, 939524096);   /* 0x38000000 == 1.0f/32768.0f */
v5 = itos(nMax); v6 = muls(v4, v5);
return stou(v6);
```

So the source is `(u16)((float)rand() * (1.0f/32768.0f) * (float)nMax)`.
The constant `0x38000000` confirms the assumption `rand()` yields `0..32767`.

### `PtrArray` layout and semantics

- `PtrArray_ctor` zeroes two dwords: `{nCount, apItems}`.
- `_Insert(pArr, nIndex, pItem)`: negative or `>= nCount` means append;
  allocates `4*(nCount+1)` every call, copies front and tail separately.
- `_Remove(pArr, 0xFFFF, _)` means remove all; with an explicit index,
  `nCount <= 0` is a no-op.
- IDA renders the tail copy size as `4 * (0x3FFFFFFF*nIndex + nCount)`. That is
  the 32-bit form of `4 * (nCount - nIndex)`; the port uses the difference.

## Deviations, each with its reason

| Deviation | Reason |
|---|---|
| No `ExtEscape(GETVFRAMEPHYSICAL)` raw-framebuffer probe | WinCE GAPI-specific; the Win64 surface is a `CreateDIBSection` |
| No `GXGetDisplayProperties` / `GXGetDefaultKeys` / `GXKeyList` | GAPI hardware keys have no Win64 equivalent; keyboard input is handled via `WM_KEYDOWN` |
| No per-display-mode pitch sign flips (`cbxPitch = -cbyPitch` etc.) | those compensated for rotated framebuffers on specific Pocket PC panels; a top-down DIB needs none |
| No early quit on unsupported resolution | the original quit because GAPI could not scale; Win64 letterboxes at integer scale instead |
| No `SetForegroundWindow(hWnd \| 1)` | CE idiom to activate an existing instance; handler bit 1 is meaningless on Win32 |
| Integer-scale letterbox present | the original was always 1:1; scaling keeps pixel art sharp and is the only sane Win64 behaviour. Flagged as a deliberate port decision, not original behaviour |
| `rand() % 32768` guard | keeps the original `0..32767` range on CRTs whose `rand()` is wider, so content generated from the RNG matches |

## Checks run

| Check | Result |
|---|---|
| `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T ClangCL` | configure OK |
| `cmake --build build --config Release` | 0 errors, 0 warnings (`/W4 /WX`) |
| PE header | `0x8664` / `0x20b` / subsystem 2 (GUI) |
| Launch | creates window, stays alive, no crash |

## Not yet recreated (later tasks)

Asset loading, data layer, rendering, audio, UI, tactical and strategic game
logic. The scaffold only proves the toolchain, the module layout, and the
display path.

## Verified: the Win64 binary launches

Verified with the shipped data beside the executable:

```
build/src/pufo/Release/pocketufo.exe   (with lang.dat and main.dat copied next to it)
```

The process opens its window, loads the startup assets and runs its message loop:
under a 5-second timeout it is still running when the timeout kills it (exit 124),
which is the expected shape for a live GUI process. A binary that failed
`App_InitInstance`, could not open the archives or fell out of the message loop
would exit on its own well before that.

This satisfies the "the binary launches on Win64" success criterion at the
scaffold level. The main playable flows are covered by
`docs/parity/game-core.md` and the remaining UI task.
