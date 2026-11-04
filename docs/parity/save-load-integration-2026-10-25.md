# Save/load and redeployment integration

Status: the private executable campaign passes. This is evidence for the stated
flow, not a full-game parity claim. All original behavior below was checked from
ARM instructions and operands; IDA comments were not used as proof.

## Confirmed faults and repairs

| Fault | Raw evidence | Repair and observable result |
|---|---|---|
| Startup teardown changed only the app's screen copy | `0x38E28` loads 22; `0x38E2C` stores the shared Map screen word. Load tests that word at `0x44CB0`. | Publish the teardown value to the world copy too. Fresh load reports app/world screen 22/22. |
| Cold load had no active world or spawn/nav contexts | `0x7E490` unconditionally branches to event processing. The port's callback and owner existed only after New Game. | Shared setup binds the owner and contexts after load/rebuild. Real loaded menu clicks, a clock-generated arrival and launch now work. |
| Smap/Strateg records were separate from live state | Smap base literal is `0x1863E8`; writes at `0x3FA74/0x3FA84` use offsets `+0x24/+0x28`. Strateg base is `0x1864F8`; its read/write offsets carry IDs, type counts, selections and economy. | Stage live owners before save and join records at each read boundary, before tactical initialization or legacy migration. Sites and camera state also join their live owners. File layout is unchanged. |
| Later ship constructors erased craft 0 cargo | `Save_ReadShips` sets the count at `0x408E0` and reads it at `0x408EC`. `Ship_ctor` reads that live count at `0x45C94/0x45C98` and clears its cargo column at `0x45CB8..0x45CD0`. | Supply the saved count to the constructor context before the loop. The saved rifle quantity 1 remains 1 after fresh-process load. |
| Team selection stopped at a rejected soldier | Craft rejection at `0x39840` and base-defense rejections at `0x398C4/0x398D0` reach `0x398F8`. The index/pointer advance and backward branch are `0x3990C/0x39910/0x39918`. | Continue the roster walk. All seven assigned survivors deploy even with an unassigned soldier between them. |

The Smap/Strateg map has an independent raw review in
`.workers/luna-gear/save-join-review.md`; its full field list is in
`.workers/luna-save-joins/RESULT.md`. The active funding multiplier at
`0x18657C` is absent from the original Strateg save header and remains unsaved.

## What the checks prove

- `test_save` loads two ships with a stale zero constructor count and nonzero
  early cargo. It checks both loaded columns, saved slot IDs, the constructor's
  unused-column clear and type-counter changes. A one-ship test hid the erasure.
- `TestProgressionWorld` poisons wire and live state, reloads through the real
  world loader, then checks fields and a later base ID allocation. It is a
  same-world test; it does not by itself prove cold-load context setup.
- `test_tactctl` uses interleaved accepted/rejected soldiers in craft modes
  0/2/3 and base-defense mode. Later eligible soldiers must still be selected.
- `tools/e2e_campaign.sh` runs a fresh process against the private save. It
  checks base/crew/funds, nonempty byte-safe roster/loadout/cargo tuples, loaded
  pointer navigation, a real arrival, and both launch/deployment crew counts.
  Latest verified log: `.workers/luna-parent/phase2-campaign-verified.log`.

The first sortie uses real inventory, move, fire, prime/fuse/throw, enemy turn,
abort, debrief and status acknowledgement paths. Its contact and initial cargo
are staged with production factories. Timer bodies are driven by the script;
this is separate from the native OS window probe.

Mission type and Map+0x1511E belong to `TGameData`. Raw `SaveGame` gates that
writer on the tactical flag; its call is at `0x44AA4`. A strategic save does not
carry those fields. Matching their unchanged startup defaults was not evidence
of persistence. The strategic tuple now excludes them.

The runner keeps strict cargo and assignment checks. It does not treat a zero
exit code, empty grep result, or malformed shell test as a successful check.
