# World callback and panel cleanup review

Status: bounded source/raw review complete. The panel cleanup repair and Debug
heap fixture compile in both configurations. They have not been run. All local
runtime testing remains disabled; verification needs another machine.

## Callback scan result

A name-based scan found19 called callback names without a named production
assignment. This is a candidate list, not proof of missing features:

| Group | Actual connection |
| --- | --- |
| 15 audio names | The positional g_pufoAudioWinmm initializer fills the backend table. This checks bindings, not every audio behavior. |
| ShipSupplyHooks.pfnLowSupplyMsg | Geoscape_TickHourly constructs the positional supply hooks with Supply_LowMsg. It forwards status43 through geoHooks.pfnContactMsg, installed as World_HookContactMsg, which appends the real message line. |
| TactShutdownHooks.pfnPanelFreeLines | World_FreeTactical calls UiPanel_FreeLines directly for the World-owned panel records. The old TactShutdownEnv heap-layout adapter is not the World owner. |
| TactShutdownHooks.pfnControlFreePads | World_FreeTactical directly frees widget pads and the inline sTactLookCtl pads. It must not free that inline control as an original heap record. |
| MapTerrainEnv.pfnResolveShot | A real missing World connection, repaired in the [terrain review](terrain-construction-resolver-2026-10-28.md). |

The low-supply raw call is0x4608C (word bytes c05000eb), with message43 loaded
at0x46078, ship home-base byte at+3 and slot byte at+0x1C. The failure flag is
set at0x4606C. Address0x460AC is a literal, not the message call. The original
look-control pad release is0x3DC9C (8971ffeb), before the heap-record delete.
The port's inline control changes ownership, not the need to free its pads.

This closes the scan's candidates. Common callback names can occur in several
environments, so the scan does not prove every instance or every branch is wired.

## Missing panel-count reset

The ownership review found a separate omission. UiPanel_ctor allocates all line
buffers for the constructor count. World tactical panels allocate14 lines for
slots0..3 and28 for slot4. UiPanel_FreeLines only frees up to nLineCount; that
field can also describe current rows. Freeing the record after a smaller count
can lose pointers to allocated hidden rows.

The original explicitly restores counts before freeing:

| Raw site | Evidence |
| --- | --- |
| 0x3D584 | 0e10a0e3: MOV R1,#14 |
| 0x3D5A0 /0x3D5AC /0x3D5B8 | Store that count at each panel+0xBDC |
| 0x3D5C4 | Store saved14 into the fourth panel+0xBDC |
| 0x3D5BC /0x3D5D4 | Load28 and store it into the fifth panel+0xBDC |
| 0x3D5E8 | Call UiPanel_FreeLines |
| 0x3E8D8 /0x3E8FC | The free loop reads that same count |

World_FreeTactical omitted those count stores. It now restores the allocation
count using the existing shutdown constants before calling UiPanel_FreeLines.
No panel schema, allocation size or general UI destructor behavior changed.

## Validation and current scope

TestTacticalPanelRelease creates a private zeroed World before asset startup,
allocates the five panels, makes their visible counts zero, releases tactical
ownership, and compares Debug CRT heap snapshots. It tests that all owned line
buffers are released, without intercepting individual free calls. Release skips
the Debug-only heap check. The fixture has been compiled, not executed.

Debug and Release game/test targets compile with zero warning/error matches.
Logs: [Debug](../../.workers/luna-callback-closure/debug-compile-with-heap-check.log)
and [Release](../../.workers/luna-callback-closure/release-compile-with-heap-check.log).

These are the cleanup snapshot hashes, before the later
[restart-layer lifetime repair](mission-init-bindings-2026-10-28.md).

| Cleanup snapshot | SHA-256 |
| --- | --- |
| World source | B484F11343EBCEF01E8C8A530C45B582526D8D192FEB1EA98462DA48CEF0B0FF |
| World test source | 053E35A01BF54E08BD4B2F1218E520BA7C8A17485504F9FB138F7C897CB30A9E |
| Debug EXE | 4F0091369BF63152207A3FC917D2448ED7DEC521B56081F55B750697723D1648 |
| Debug PDB | 5F3E08F331D02CCEC6FF19DAC2C840E8D64DD7232D3E53FEAC523DED25097AC3 |
| Release EXE | E25F0E9B0A6DCBA612407B44F521EF65CA5410C57C755C37CEA82D17403D73CC |

The terrain repair's recorded241 damaged cells predates this cleanup change.
The independent PSI setup and this heap check still need runtime validation.
Earlier67/67 and native passes remain tied to their older images.
