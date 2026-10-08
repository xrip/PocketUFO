# PocketUFO IDB Map — `pufobeta.exe`

Source of truth: `E:\pocketufo\pufobeta.exe.i64` (IDA Pro 9.4, ida-pro-mcp 3.4.0).
Generated for the Win64 recreation goal. Regenerate with `group_inventory.py`.

## Binary facts

| Item | Value | Evidence |
|---|---|---|
| Input | `pufobeta.exe` | `server_health` |
| Image base | `0x10000` | `server_health` |
| Functions | 1103 | `idautils.Functions()` |
| Strings | 713 | `server_health` strings cache |
| Entry point | `start` @ `0xa1534` | `ida_entry` enumeration |
| C runtime main | `WinMain` @ `0x9fcc4` | `InitInstance` @ `0x9fd48` |
| Window proc | `PocketUFO_WndProc` @ `0xa03ec` | name + Win32 message loop |
| Platform | WinCE, ARM (32-bit, `__fastcall`/`__thiscall`) | imports + decompile |

## Segments

| Start | End | Name | Content |
|---|---|---|---|
| `0x11000` | `0xa2000` | `.text` | game and UI code |
| `0xa2000` | `0xa3000` | `pocketuf` | software blitter inner loops |
| `0xa3000` | `0xa5000` | `screen_w` | blitter state + surface blit helpers |
| `0xa5000` | `0xa6000` | `.rdata` | window class name, import descriptors |
| `0xa6000` | `0xa61e0` | `.idata` | imports |
| `0xa61e0` | `0x189000` | `.data` | globals + embedded data tables |
| `0x189000` | `0x18b000` | `.pdata` | exception directory |

## Import modules

| Module | Kind | Used for |
|---|---|---|
| `COREDLL` | WinCE CRL + Win32 subset | files, registry, timers, windowing, CRT, GDI |
| `AYGSHELL` | Pocket PC shell | `SHHandleWMActivate`, `SHHandleWMSettingChange` |
| `gx` | GAPI (raw framebuffer) | `GXOpenDisplay`, `GXBeginDraw`, `GXEndDraw`, `GXGetDisplayProperties` |
| `fmodce` | FMOD CE | `FSOUND_*` sample playback, `FMUSIC_*` module music |

Win64 port note: `gx` disappears (use a real Win32 surface/D3D path or a raw blit),
`COREDLL` → `kernel32`/`user32`/`gdi32`/`advapi32`, `AYGSHELL` → no-op stubs,
`fmodce` → FMOD core kept behind the `Sound_*` interface.

## Subsystems (all 1103 functions classified)

| Subsystem | Funcs | Bytes | Role |
|---|---|---|---|
| `tact` | 214 | 178,344 | tactical battle: map, movement, LOS, combat, units, AI |
| `strat` | 205 | 162,456 | geoscape: globe, radar, ships, bases, funding, strategy list |
| `strat-ui` | 243 | 117,008 | base/crew/manuf/research/buysell/transfer/options/ufopaedia screens |
| `tact-ui` | 52 | 43,764 | tactical HUD, inventory, command pads, input dispatch |
| `ui-core` | 85 | 38,368 | `UiPanel` text engine, modals, pads, fonts, scrolling lists |
| `save` | 35 | 27,172 | save/load chunks, slot formatting, roster indices |
| `render` | 51 | 11,108 | software surfaces, blitters, RGB565 packing, palette load |
| `app-shell` | 24 | 6,376 | `WinMain`, window class, registry options, timers |
| `titles` | 13 | 4,444 | intro/outro animated scenes |
| `boot-assets` | 4 | 2,744 | startup asset load and shutdown |
| `dat` | 18 | 2,744 | `DatFile` tagged archive + `AssetReader` |
| `platform` | 132 | 1,908 | CRT start/exit, import thunks, GDI thunks |
| `util` | 21 | 1,180 | `PtrArray`, atexit banks, RNG, math consts |
| `audio` | 6 | 1,176 | `Sound_*` wrapper over FMOD |

Full per-function listing: `inventory-grouped.tsv`.

## Verified container and data formats

These were checked against decompilation, not assumed.

### `DatFile` — tagged archive (`dat` subsystem)

Evidence: `DatFile_Open` @ `0x9ef38`.

- Header: 4-byte little-endian entry count, then the directory.
- Directory entries: 21 bytes each (`DatDirEntry`), `szName` is a NUL-terminated name.
- Modes: `1` = read, `2` = create, `3` = rewrite.
- Chunk access: `DatFile_BeginChunk` @ `0x9f348`, `DatFile_FindChunk` @ `0x9f5ac`,
  `DatFile_Read` @ `0x9f698`, `DatFile_Write` @ `0x9f4b4`, `DatFile_FlushWrite` @ `0x9f234`.
- `AssetReader` @ `0x9f7e4` wraps chunk lookup for the many `*.dat` members.

### `.rmp` route nodes (tactical pathing)

Evidence: `Map_LoadRmp` @ `0x55060`.

- Records are 24 (`0x18`) bytes; node count = member size / 24.
- Per record: byte0 = local Y, byte1 = local X, byte2 = height, bytes 4..6 per link,
  byte 19 and byte 20 are flags.
- Link sentinels: `251`/`254` = north edge, `252` = south edge, `253` = east edge,
  `255` = no link. Other values are relative node indices.
- World coords: `nX = 10 * tileX + localX`, `nY = 10 * tileY + localY`,
  `nZ = level - height - 1`.
- Allocated node is `RmpNode`, `0x1C` bytes.

### Asset names (from `.data` string anchors)

- Archives: `main.dat` @ `0xa64d4`, `lang.dat` @ `0xa64c0`.
- Members referenced by name: `texts.dat`, `fontsize.dat`, `objects.dat`,
  `unitloft.dat`, `loftemps.dat`, `lightmap.dat`, `showship.dat`,
  `research.dat`, `v124.dat`.
- Palettes: `soldier.pal`, `class.pal`, `scanner.pal`, `menu.pal`, `tactical.pal`,
  `base.pal`, `radar.pal`, `geoscape` set (`earth50/75/100.pal`, `cydonia.pal`),
  per-screen sets for `baseinfo`, `baseat`, `funding`, `transfer`, `plane`,
  `manuf`, `research`, `psi`, `buysell`, `ufoflash`, `ufos`, `mapbits`, `basebits`.
- Pads: `titles0..4.pad`, `badend.pad`, `goodend.pad`, `emptyb.pad`, `startg.pad`,
  plus per-screen pad sets.

### Save chunk tags (from `.data` tag strings)

`strateg`, `bases`, `product`, `research`, `ships`, `smap`, `time`, `soldiers`,
`tactflg`, `flobjs`, `map`, `teamunits`, `rmp`, `tgamedata`, `gamestate`,
`hiddensold`, `diedsolds`, `aliens`.

Save format version global: `nSaveFmtVer` @ `0x18655c`.

## Key globals for recreate work

| Address | Name | Role |
|---|---|---|
| `0x1139b8` | `pCtxBase` | map context (dimensions, tiles) |
| `0x156938` | `pMap` | tactical map |
| `0x158890` | `gUiRootPanel` | root UI panel |
| `0x158268` | `fontSys` | system font |
| `0x159618` | `nGameTimeSec` | game clock |
| `0x159d80` | `nBaseCount` | base count |
| `0x186534` | `strategList` | geoscape strategy object list |
| `0x186530` | `nStrategCount` | strategy object count |
| `0x1863e8` | `gSmapCam` | geoscape camera |
| `0x186566` | `bRadarScreenOn` | radar screen state |
| `0x187058` | `nTimerC354Cnt` | timer counter |
| `0x187f30` | `renderTarget` | active render surface |
| `0x188d40` | `nScreenW` | screen width |
| `0x188d50` | `nScreenH` | screen height |
| `0x188d54` | `hWndMain` | main window handle |
| `0x1883b0` | `szAppDir` | application directory |

## Highest-risk functions for the port

Ranked by size and by how much of the game depends on them.

| Address | Size | Name | Risk |
|---|---|---|---|
| `0x27bb8` | 14,716 | `Unit_CanStepTo` | largest tact function; movement rules |
| `0x22e10` | 10,676 | `Map_BuildMissionTerrain` | map generation from templates |
| `0x9b5f4` | 10,196 | `Ui_DrawBaseInfo` | base screen drawing |
| `0x872d0` | 8,576 | `Geoscape_OnPointer` | geoscape input |
| `0x50adc` | 8,192 | `Tact_ProcessFireAction` | shooting resolution |
| `0x8c180` | 33,744 | `Geoscape_Draw` | largest function in the binary |
| `0x557f4` | 6,084 | `BaseInfoScreen_BuildPanels` | panel layout from data tables |
| `0x632cc` | 6,096 | `Ui_TextInputDialog` | modal text input |

## Open questions

- `0x1139b8 pCtxBase` field layout is only partly recovered; needs struct work
  before the tact subsystem can be ported cleanly.
- `RmpNode` layout (`0x1C` bytes) is inferred from `Map_LoadRmp` only. Confirm with
  the consumers (`Map_PickRmpSpawnForUnit` @ `0x55418`).
- Whether `gx` GAPI can be replaced by a straight Win32 blit depends on the display
  property flags read by `GXGetDisplayProperties`; the Win64 path needs a decision.
- Save chunk `v124.dat` / `nSaveFmtVer` version handling must be reproduced exactly,
  or the original save files will not load.

## Regenerate

```bash
cd docs/idb
# refresh functions.tsv from IDA (py_eval in the MCP session), then:
python group_inventory.py
```
