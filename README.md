# PocketUFO — Win64 recreation

A from-scratch recreation of **PocketUFO** (`pufobeta.exe`, a Windows CE / ARM32
game) as a native **Win64** executable, built in C23. Every behavior is traced
to the IDA Pro decompilation of the original binary (`pufobeta.exe.i64`, 1103
functions); the project goal is behavior parity across every subsystem:
startup/config, data loading (`main.dat`, `lang.dat`, saves), world/map,
entities, tactical combat, strategy layer, UI/input, rendering and audio.

See [PARITY.md](PARITY.md) and [docs/](docs/) for the parity evidence, and
[docs/BUILD.md](docs/BUILD.md) for the full build notes.

## Download and run

Get the latest release zip from the [Releases](../../releases) page. The zip is
self-contained:

```
pocketufo.exe            the game (x64 GUI app)
main.dat                 original game data (required)
lang.dat                 original text/table data (required)
libopenmpt.dll           music decoder (BACK*.IT modules)
openmpt-mpg123.dll
openmpt-vorbis.dll
openmpt-ogg.dll
openmpt-zlib.dll
libopenmpt-licenses/     third-party license texts
```

Put all files in one folder and start `pocketufo.exe`. The game reads
`main.dat` and `lang.dat` from its own directory; original data files work
as-is. Save games are written next to the exe (`sav0.sav` …).

Requirements: 64-bit Windows 10/11 and the *Visual C++ 2015–2022 Redistributable
(x64)* (used by the music decoder). The original WinCE DLLs (`fmodce.dll`,
`gx.dll`) are **not** needed — audio runs on WinMM + libopenmpt, and the GAPI
display layer is recreated natively.

## Build from source

Toolchain (as verified): CMake ≥ 3.25, Visual Studio 2022 BuildTools with the
*ClangCL* toolset (Clang 23.x), Windows SDK 10.

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -T ClangCL
cmake --build build --config Release
```

Output: `build/src/pufo/Release/pocketufo.exe`. The first configure fetches a
pinned libopenmpt 0.8.9 Win64 package (SHA-256 checked, see
[cmake/PufoOpenMPT.cmake](cmake/PufoOpenMPT.cmake)); the build copies the
runtime DLLs beside the exe.

The build enforces C23 and `/W4 /WX` — warnings are errors.

## Tests

67 test suites run under CTest (one per recreated module):

```bash
ctest --test-dir build -C Release -j 8
```

`tools/check.sh` is the full pre-commit loop: text-table regeneration check,
Release + Debug builds, the full test run, and the parity report.

## Repository layout

```
CMakeLists.txt / cmake/     build setup, pinned libopenmpt fetch
src/pufo/                   game sources, mirroring the IDB subsystem map
  app/ platform/ util/ dat/ render/ audio/ ui/ game/
tests/                      one suite per module (CTest)
tools/                      check/iter/e2e runners, table generators
docs/                       build notes, IDB maps, parity evidence
PARITY.md                   generated per-module parity report
```

## Notes and limits

- The data files (`main.dat`, `lang.dat`) are assets of the original PocketUFO
  game and belong to their author; this repository contains only the recreated
  client code.
- WinCE-only behavior with no Win64 equivalent (soft keyboard, stylus-only
  quirks) is adapted, not simulated; each adaptation is recorded in
  `docs/parity/`.
- Debug configuration tracks two suites that are red at HEAD
  (`test_titles`, `test_plane`); Release is the gate.
