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
src/pufo/CMakeLists.txt        libraries, the exe, and one target per test suite
src/pufo/pufo_common.h         base types + DisplayMode (verified @ 0x9fd48)
src/pufo/app/
  pufo_app.c                   wWinMain + WndProc                     [app-shell]
src/pufo/platform/
  pufo_gx.{h,c}                GAPI (gx.dll) replacement             [platform]
  pufo_reg.{h,c}               registry options                      [platform]
src/pufo/util/
  pufo_ptrarray.{h,c}          PtrArray                              [util]
  pufo_rand.{h,c}              Game_RandU16                          [util]
  pufo_str.{h,c}               string helpers (pufo_strcpy & co)    [util]
src/pufo/dat/
  pufo_dat.{h,c}               DatFile container + AssetReader       [dat]
src/pufo/asset/
  pufo_asset.{h,c}             asset lookup layer                    [boot-assets]
src/pufo/render/
  pufo_render.{h,c}            software surface, blitters, palette   [render]
src/pufo/audio/
  pufo_audio.{h,c}             FMOD-backed sound behind a seam       [audio]
src/pufo/ui/
  pufo_panel.{h,c}             UiPanel                               [ui-core]
  pufo_control.{h,c}           controls, list boxes, buttons         [ui-core]
  pufo_modal.{h,c}             modal dialog plumbing                 [ui-core]
src/pufo/game/                 the tactical + strategy core (48 modules)
  pufo_mapcell pufo_mapctx     cells, and the live Map context       [tact]
  pufo_unit pufo_roster        soldiers, stats, rosters              [tact]
  pufo_tactstate pufo_turn     action queue, turn flow               [tact]
  pufo_path pufo_los pufo_vistry   pathing, LOS, ray table, FOV field   [tact]
  pufo_ballist pufo_combat     aim, shot arcs, damage                [tact]
  pufo_tactctl pufo_tactui pufo_cursordir pufo_input pufo_isodraw    [tact-ui]
  pufo_font pufo_menu pufo_statusmsg pufo_soldiername pufo_baseui    [tact-ui]
  pufo_objectrec pufo_ship pufo_template pufo_rmp pufo_options       [tact]
  pufo_save pufo_boot pufo_clock pufo_timer pufo_enemyai             [save / boot-assets]
  pufo_geoscape pufo_strateg                                         [strat]
tests/                         one suite per module + pufo_test.h
```

`src/pufo/game/` is one directory because the IDB groups these functions under
`tact`, `tact-ui`, `save` and `strat` but they share the `Map` context; the
subsystem label is written in each file's header comment instead.

## Test harness

`tests/pufo_test.h` is the shared harness. Every suite prints one line per check
and one summary line, in this fixed format:

```
  ok   <what>
  FAIL <what>: got N want M
test_name: 83 checks, 0 failures
```

`docs/parity/gen_report.py` reads the summary line, so `PARITY.md` reports a real
check count for every suite. Suites that still have their own local helpers keep
their call-site argument order by wrapping the shared `TestCheck` / `TestEqI32` /
`TestEqU16` / `TestNear` / `TestSummary` helpers.

## Windows subsystem note

`pocketufo` sets `WIN32_EXECUTABLE TRUE`. The original is a GUI app
(`WinMain` @ `0x9fcc4`), so the entry point is `wWinMain`. Without this property
`lld-link` fails with `undefined symbol: main`.

## Seed determinism warning

`Game_RandU16` @ `0x3e59c` depends on `rand()` returning `0..32767`. MSVC's CRT
does that; other CRTs do not. `pufo_rand.c` reduces the result modulo 32768 to
keep the original distributions. See the comment there.
