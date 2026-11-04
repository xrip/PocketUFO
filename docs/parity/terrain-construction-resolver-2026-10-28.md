# Missing terrain-construction shot connection

Status: production connection repaired; current Debug/Release sources compile.
The new independent mind-control setup still needs runtime verification on
another machine. No local game or test execution is authorized.

## Missing behavior

The terrain builder had its damage loops and the combat resolver existed, but
World_InitTactical cleared the terrain environment and never installed
MapTerrainEnv.pfnResolveShot. Init_FillCalleeEnvs filled the other terrain fields
but left this callback null. Both terrain-shot calls were therefore skipped in
the real World path. Citation coverage and leaf tests did not catch the gap.

## Raw evidence

The original Map_BuildMissionTerrain function spans0x22E10..0x257C4.
The relevant raw words, shown as little-endian bytes, are:

| Site | Bytes | Effect |
| --- | --- | --- |
| 0x22E20 | 0040a0e1 | Copy incoming Map pointer R0 to R4 |
| 0x22E28 | 4c418de5 | Save R4 at SP+0x14C |
| 0x23C44 | 04308de5 | Store selected object pointer as stack argument at SP+4 |
| 0x23C48..0x23C50 | 0030a0e3 / 00308de5 / 0030a0e3 | Zero slot stack argument and layer R3 |
| 0x23C54 | 342d00eb | BL0x2F12C, Combat_ResolveShot |
| 0x23D48 | 4c319de5 | Reload saved Map pointer from SP+0x14C |
| 0x23D5C | 083193e5 | Read object0 at Map+0x108 |
| 0x23D64..0x23D70 | 04308de5 / 0030a0e3 / 00308de5 / 0030a0e3 | Pass that object, slot0, layer0 |
| 0x23D74 | ec2c00eb | BL0x2F12C again |
| 0x2F24C /0x2F254 | 0539a0e3 /036084e7 | Store the weapon into Map+0x14050 |

Names are navigation aids; the words, register setup and saved-pointer data flow
are the evidence. This review covers the missing call connection and live
weapon store, not every branch in the terrain builder or combat resolver.

## Repair

World_TactTerrainShotThunk calls the existing Combat_ResolveShot with the cells
being built, current geometry/origin, the already-created tactical state and
roster view, the normal World FOV/area hooks, and both GameMap hit windows. It
publishes the weapon to GameMap.pFireArg6, matching the raw store above.
World_InitTactical installs the thunk before Tact_InitMission.

The state constructor and visibility-ray table setup precede terrain building.
Team selection and alien spawning follow it. The thunk does not mark a mission
live early or use the later live-turn context as a substitute.

Resolver-only World snapshot SHA-256, before the later
[panel cleanup repair](world-callback-cleanup-2026-10-28.md):
E93332198A27D89AC2272D4D4E4322444C278F857ABBF93902AA46F96059AAF8.

## Tests and limits

The existing World fixture seeds a damaged class-2 UFO at50/250 HP and uses the
real intercept, arrival, launch-control and mission setup paths. It now checks
nonzero cell face damage and the final object0 firing context. It does not just
check that a function pointer is non-null.

Before the repair, Debug and Release each fail exactly those two new checks:
variant2, damage band70, damagedCells0. Old binaries, source preimages and logs
are retained under [.workers/luna-terrain-resolver-fix](../../.workers/luna-terrain-resolver-fix).
The repaired Debug run records damagedCells241 and passes both new checks.
The count241 is an observation, not a hardcoded expected result.

That run still fails a following mind-control fixture's precondition: it reuses
the damaged mission and assumes at least two aliens survived. Restoring the
real terrain resolver also restores its RNG use and changes later spawn state.
The previous fixture did not guarantee that survivor count.

The setup is now shared in FrameLaunchWorldUfoMission: terrain/spawn cleanup
uses50 HP; mind control builds its own seeded250-HP mission. Its AI behavior
section is byte-for-byte unchanged from the pre-isolation source: visibility,
real AI forwarding, command18/mode5, AP, side transfer and restoration checks
remain. This setup change is compile-checked only and must not be called a
runtime pass yet. PSI-isolation test snapshot hash, before the added heap check:
DF0230DEC05C4DF808E0F5172EA45C9F6C05CA08BB783EDE97FBAA4DF40F2EC6.

Debug and Release test targets compile with zero warning/error matches. The
game targets also compile and link in both configurations. No executable was
run for those compile checks. Logs are `psi-isolated-*-compile.log` and
`game-*-compile.log` in the fix folder. The earlier67/67 full-suite result
predates this terrain repair and does not validate the current source.
