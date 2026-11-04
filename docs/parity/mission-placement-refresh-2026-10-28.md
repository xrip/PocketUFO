# Mission placement and craft sprite refresh

Status: source repaired; Debug/Release compile-only checks pass. Runtime pending.
No game, test executable or debugger was launched for this change.

## Defects and changes

`Init_FillCalleeEnvs` copied the pre-generation craft coordinates into the team
selection and persistent deployment contexts. Terrain generation then replaced
the coordinates without updating those copies. Both contexts now refresh after
terrain generation, before crew selection, and after saved tactical state loads.
The refresh includes the layer selector used by both consumers.

Fresh terrain construction also retained the craft sprite base captured before
the sprite-loading hook. It now reads flag group 3 from the live font immediately
before craft patching. Saved mission patching likewise reads the live font after
the saved tactical data and RMP have loaded, rather than copying the pre-load cache.

## Raw ARM evidence checked

- `0x39764` / `0x39768` (`40c09fe5`, `0c20d4e7`) load the craft Y address
  and byte at crew selection; `0x39784` / `0x39788` (`18c09fe5`, `0c20d4e7`)
  load craft X. These are reads of current storage, not saved pre-generation copies.
- Deployment reads the coordinate bytes at `0x39EF0` and `0x39F14`
  (both `0c10d5e7`, `LDRB R1,[R5,R12]`).
- Craft patching compares the layer byte with 2 at `0x25AC8` (`020053e3`)
  and branches out otherwise at `0x25ACC` (`e200001a`). `0x25AD0`
  loads the font block address and `0x25AD4` (`b600d4e1`) reads its current
  u16 at offset 6, followed by addition of sprite offset 13 at `0x25AD8`.
  Offset 6 follows from instruction encoding, independent of IDA field names.

## Focused checks added, not executed

- `test_tactinit`: the deployment fixture starts with invalid craft coordinates
  (255,255), then requires both crew-selection and later deployment contexts to
  match terrain's generated coordinates and layer selector. This checks the
  caller-to-consumer binding; it does not prove full crew gameplay or saved loads.
- `test_terrain`: the sprite-loading hook changes the craft base from 0 to 32.
  The test requires flags cleared on sprites 74 through 77 and a real map wall
  stamp pointing to sprite 74. This checks the post-load patch outcome rather
  than only checking a cached value. It does not exercise the saved-mission path.

## Build evidence and candidate identity

Both commands returned exit 0, with zero compiler warning/error diagnostics:

```
cmake --build build --config Debug --target pocketufo test_geoscapeframe test_tactinit test_terrain --parallel 2
cmake --build build --config Release --target pocketufo test_geoscapeframe test_tactinit test_terrain --parallel 2
```

Logs: [Debug](../../.workers/luna-mission-init-audit/debug-bindings-compile.log),
[Release](../../.workers/luna-mission-init-audit/release-bindings-compile.log).

| Artifact | SHA-256 |
| --- | --- |
| pufo_tactinit.c | 249655E8FB23E27C1C8AF337D8B23C4F377B2C2314A51EEE905377481C8481CD |
| pufo_terrain.c | 1E4D4AA241228F5454C8AF8D4396FC647D464CF450422444227840BECDF79656 |
| test_tactinit.c | E91D41B7321D292B675E51E8D0A7E5186C9C5AEAF73B909AEBCB3DB879D576AE |
| test_terrain.c | 33E039C7AFD03E0526A42C8B35C0B3362FF522B0AD914547A3753973C5A89230 |
| Debug EXE | 03F03404699B9D099A8D5BDFFF2B6D2FC97027CA3D4EB134D730DCD2C2ED62A5 |
| Release EXE | A40A3B8EE3230967BEE2ABEACA8138DC7F97732C7E824F7DE4827C2F84D6AF14 |

These identities supersede the executable hashes in the earlier mission-init
audit for current-build purposes. They establish compiled artifacts, not runtime
success. Saved crew deployment, cold load craft appearance, the focused checks,
and the complete gameplay matrix still need an authorized Windows test host.

Source review caught a fixture conflict before runtime: its default font group
is deliberately outside the sprite pool to suppress template stamping. The new
loading hook now changes that base only when explicitly enabled by its regression.
The existing craft-patch case enables base 0 explicitly. Other cases retain their
template isolation. Both configurations were recompiled after this correction.
