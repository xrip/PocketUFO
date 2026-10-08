# Build — PocketUFO Win64 recreation

## Toolchain (verified on this machine)

| Tool | Version | Path |
|---|---|---|
| CMake | 4.2.3 | `cmake` |
| Clang | 23.1.1 (x86_64-pc-windows-msvc) | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang-cl.exe` |
| MSVC toolset | 14.44.35207 | VS 2022 BuildTools |
| Windows SDK | 10.0.26100.0 | `C:\Program Files (x86)\Windows Kits\10` |
| Ninja | available | `C:\Python314\Scripts\ninja.exe` |

## Configure and build

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T ClangCL
cmake --build build --config Release
```

Output: `build/src/pufo/Release/pocketufo.exe`

## Verified properties

- `CMAKE_C_STANDARD 23`, `CMAKE_C_EXTENSIONS OFF` — `__STDC_VERSION__ == 202311`.
- `/W4 /WX` via the `pufo_options` interface target. Warnings are build errors.
- Output PE: `machine=0x8664` (x64), `magic=0x20b` (PE32+), `subsystem=2` (GUI).
- The exe launches, creates its window, and stays alive (no crash on startup).

## Source layout

The tree mirrors the IDB subsystem map (`docs/idb/inventory-grouped.tsv`).
Directories are created as each subsystem's task starts; nothing speculative.

```
CMakeLists.txt
src/pufo/
  pufo_common.h            base types + DisplayMode (verified @ 0x9fd48)
  platform/                WinCE -> Win32 seams
    pufo_gx.{h,c}          GAPI (gx.dll) replacement  [scaffold]
  util/                    shared containers and helpers
    pufo_ptrarray.{h,c}    PtrArray                     [scaffold]
    pufo_rand.{h,c}        Game_RandU16                 [scaffold]
  app/
    pufo_app.c             wWinMain + WndProc          [scaffold]
```

Subsystems still to be added: `dat`, `save`, `ui-core`, `tact`, `tact-ui`,
`strat`, `strat-ui`, `render`, `audio`, `titles`, `boot-assets`.

## Windows subsystem note

`pocketufo` sets `WIN32_EXECUTABLE TRUE`. The original is a GUI app
(`WinMain` @ `0x9fcc4`), so the entry point is `wWinMain`. Without this property
`lld-link` fails with `undefined symbol: main`.

## Seed determinism warning

`Game_RandU16` @ `0x3e59c` depends on `rand()` returning `0..32767`. MSVC's CRT
does that; other CRTs do not. `pufo_rand.c` reduces the result modulo 32768 to
keep the original distributions. See the comment there.
