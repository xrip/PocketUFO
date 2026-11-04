# Large-unit corpse object slots

## Finding

The original drops three extra corpse items for each large unit. The port's corpse-quadrant table had only 48 halfwords, covering unit types 0–11. For large types 18, 20, and 21, its bounds check returned object slot 0 for every extra item.

The source table is now 88 halfwords, copied from the original 22-by-4 table. The existing bounds check stays in place. The primary corpse mapping is unchanged.

## Original evidence

The loaded `OBJECTS.DAT` member in `main.dat` is 30,992 bytes: 149 records at 208 bytes each. The startup ARM block uses record size `0xD0`, stores the first object pointer at `Map+0x108`, and advances the record and pointer slots by `0xD0` and 4 (`0x38418`, `0x38478..0x3849C`). Thus a raw slot ID `N` selects `Map+0x108+4*N`.

In `Tact_KillUnit`, the raw primary-corpse path at `0x4E9D0..0x4E9F8` loads the unit type, reads a halfword from the primary table at `0xA9260`, scales that slot by four, and loads the object pointer at map offset `0x108`.

The raw quadrant table is at `0xA7970`. The ARM literal at `0x4EC94` is `0x000A7970`; `0x4EB50` loads it. The code indexes the row by `unit_type * 8`, reads q1/q2/q3 at offsets `+2` (`0x4EB6C`), `+4` (`0x4ECC4`), and `+6` (`0x4EE18`), then loads each object pointer through `Map+0x108` (`0x4EB7C`, `0x4ECD4`, `0x4EE28`).

The large-unit predicate reads the byte at row offset `+1`, with four-byte rows (`0x49ECC..0x49EE8`). Raw flags at `0xA7B80` mark unit types 18, 20, and 21 as large. The death path calls that predicate at `0x4EAE4` and enters the three additional corpse paths when it returns true.

The original quadrant rows and live data slots are:

| Unit type | Original row | `OBJECTS.DAT` meanings |
|---:|---|---|
| 18 | `99, 134, 135, 136` | Cyberdisk; three disk corpse parts |
| 20 | `66, 131, 132, 133` | Reaper corpse; three Riper corpse parts |
| 21 | `69, 137, 138, 139` | Sectopod remains; three Sectopod corpse parts |

These raw table values match records in the shipped member. The current `main.dat` hash is `B97DE23D47C3F02393FEA968A826413BB3F7054E79661B211B244BCBE9406B7A`.

## Port change and regression

`src/pufo/game/pufo_unit.c` now carries all 22 rows from `0xA7970`. In particular, the three affected rows are `{99,134,135,136}`, `{66,131,132,133}`, and `{69,137,138,139}`.

`tests/test_unit.c` adds `TestKillLargeCorpseObjectSlots`. It drives `Tact_KillUnit` for types 18, 20, and 21, checks all four resulting item object pointers against the raw slot IDs, and asserts none falls back to slot 0. The test chooses a random seed that suppresses the Cyberdisk's separate blast so it checks only corpse creation.

At the worker handoff, the regression had not been run. The later Debug build
was clean and `test_unit` passed, including all three new death cases:
[batch evidence](../../.workers/luna-runtime-corrected-port/RESULT.md).
This proves the bounded corpse regression, not every combat outcome.

Parent raw read-back: all 88 source halfwords compare equal to the 176 IDB bytes
at `0xA7970`. Instruction bytes `83 31 88 E0` at `0x4EB68` add `unitType<<3`
to the table base. The quadrant loads are `B2 30 D3 E1` at `0x4EB6C`
(`LDRH R3,[R3,#2]`), `B4 30 D3 E1` at `0x4ECC4` (`#4`), and
`B6 30 D3 E1` at `0x4EE18` (`#6`). `03 31 84 E0` at `0x4EB78`
adds `slot<<2` to the map base; `08 31 93 E5` at `0x4EB7C` loads the
object pointer at `+0x108`. The same object-pointer load occurs at `0x4ECD4`
and `0x4EE28`. These raw bytes, rather than IDA comments, support the mapping.
