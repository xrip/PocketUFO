# Process initialization repeated on title return

Status: source repaired; Debug/Release affected targets compile; runtime pending.
Updated: 2026-10-28.

## Confirmed difference

App_InitStartupUi was called at initial startup, after returning from credits,
and through the return-to-title hook. Every call ran UiBank_CtorAll,
Bank_RegisterAtexit, a tactical-state memset, and Map_InitAndLoadRegistry.
These are process-initialization effects in the original. Repeating them can
replace existing control state (the port control constructor memsets each
control), re-register the same exit callback, reload options from the registry,
and reset tactical scratch during an ordinary menu transition.

Raw startup data places 0x11148 and 0x22604 in the constructor table at
0xA61E4 and 0xA61E8. Their wrappers call 0x11158 and 0x22614; those branches
reach UiBank_CtorAll at 0x11320 and Map_InitAndLoadRegistry at 0x22648.
Current raw xrefs to those two bodies are respectively 0x1115C / 0x22618
and their image metadata references at 0x189050 / 0x1896B0.

The raw return-to-title routine 0x6B46C calls 0x8B94C at 0x6B480 and
0x86D20 at 0x6B488, then branches to 0x38A28 at 0x6B490
(bytes 6435ffea). The function 0x38A28..0x38D7C calls font/asset/UI/sound
setup; its direct external branches do not call either process constructor
or their registration wrappers. For example 0x38A34 calls 0x9F7D8
(bytes 679b01eb), and 0x38A40 calls 0x460E8 (bytes a83500eb).
Thus sharing the port's process construction with every title rebuild was an
extra operation introduced by the port, not an effect demonstrated by that raw path.

## Change

Moved the bank/registry initialization block into App_InitStartupGlobals with
a process-lifetime once guard. App_InitStartupUi calls it before menu setup.
The first menu startup keeps the previous port order; later title rebuilds
skip these process effects. No generic lifecycle framework or new public API
was introduced. Moving all initialization to the exact pre-WinMain point was
not attempted; full startup ordering remains part of the larger lifecycle audit.

## Verification and limits

Compiled pocketufo, test_appshell, test_geoscapeframe, test_tactinit and
test_terrain in Debug and Release, both command exits 0. Logs:
[Debug](../../.workers/luna-mission-init-audit/debug-startup-once-compile.log),
[Release](../../.workers/luna-mission-init-audit/release-startup-once-compile.log).
No game, test or debugger executable was launched.

Required host checks: enter/leave credits twice; enter a game and return via
Options to title twice; confirm the menu still responds and a subsequent
new game/load works. Trace bank constructor, registry-load and registration
calls: one initial call each, none during those title rebuilds. Check that
current option values are retained, controls are not re-zeroed by the process
constructor, and exit releases the retained ownership without double-free/leak.
These checks are specified, not executed; existing test passes cannot prove them.

## New candidate identity

Source tree fingerprint (same 434-file algorithm as the
[previous candidate](candidate-2026-10-28-bindings.md)):
**5DA558855F74C373D5983D42F36D171A897419D259EC7C6E0E92EFC2536E98C2**.

| Artifact | SHA-256 |
| --- | --- |
| src/pufo/app/pufo_app.c | B18C9B2C42AADB4FE679E4699D27134AB9414F3D790F973BFF14B37839AC8A49 |
| build/src/pufo/Debug/pocketufo.exe | 0666ED3DB7D8B8A5886E7E1BFD74C1E445CF03DA46579A0216290D1E3F2DC665 |
| build/src/pufo/Release/pocketufo.exe | 91504C663A098C0CAE8AA65E8F49AB72E82517ED9097B42E76386A46583419EF |
| build/tests/Debug/test_appshell.exe | E12A4C1021CEB4F1FAED3DD3B79D6885AF0F025F5DFD3932BC0F5794B148CAC0 |
| build/tests/Release/test_appshell.exe | BFE378B5F2A750529A59121EFE4C66464A69EFC0CF8D6C1E6757B5C7C62AD7D8 |

This supersedes the previous candidate for runtime verification. Its game data
files remain the same. Citation locations for 48 ledger rows were refreshed
after the App source shifted. The port remains incomplete.
