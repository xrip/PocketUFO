# Parity coverage

This file used to hold a generated coverage snapshot from an older `tools/coverage.py`. That tool no
longer exists and the numbers here were badly out of date — they claimed 275 of 1103 functions
recreated while the real figure was already over 600, and they listed functions as unported that had
been ported and tested. Rather than keep a second, contradictory report, the snapshot is retired.

The live report is `PARITY.md`, regenerated with:

```
python docs/parity/coverage.py     # totals
python docs/parity/gen_report.py   # writes PARITY.md
```

The audits that keep the report honest are `docs/parity/audit_credits.py` (a port claim next to a
port denial), `docs/parity/audit_names.py` (a credited address whose original name has no definition
in the port), `docs/parity/audit_bugs.py` (code markers against `docs/parity/bugs.tsv`), and
`docs/parity/audit_tables.py` (hand-copied tables diffed against the shipped image).

## What "not cited" means: `docs/idb/image-artifacts.tsv`

135 of the 1103 original functions are never cited, and reading that as "135 functions left to
port" is wrong. Each of the 135 was opened in `pufobeta.exe.i64` and read instruction by
instruction, and the class recorded in `docs/idb/image-artifacts.tsv` is what the instructions
say, not what the name suggests:

| class | count | bytes | the shape in ARM |
|---|---|---|---|
| `import-thunk` | 85 | 1020 | `LDR R12, =__imp_X; LDR PC, [R12]` — the Win32 / CRT / MFC / fmodce entry the game calls through |
| `code-thunk` | 27 | 252 | `B target`, or `LDR R0, =pMap; MOV R1, #1; B target` — a call-site wrapper that sets arguments |
| `empty-stub` | 8 | 32 | `BX LR` |
| `init-term-entry` | 6 | 96 | `PUSH {LR}; BL ctor; POP {LR}; B RegisterAtexit` — one per subsystem, reached from a pointer-table slot |
| `crt-runtime` | 7 | 532 | `start`, `_cinit`, `_onexit`, `atexit`, `Crt_CallFnTable`, `Crt_DoExit`, `Crt_Exit` — the C runtime shipped inside the image |
| `debug-print-stub` | 1 | 16 | `Debug_TracePrint @ 0x9e0b0`: drops all four arguments; every call site has already loaded a label and `"s"` |
| `decode-artifact` | 1 | 4 | a `POP {R0-R12,PC}` word inside the `screen_w` data segment with zero cross-references |

"Carries no behaviour of its own" needs one caveat for the code thunks: only 10 of the 27 are a bare
`B target`. The other 17 load something first — `LDR R0, =pMap`, `MOV R1, #1` — and those are the
arguments handed to the callee. The wrapper adds nothing the target's own parity note does not already
have to cover, but the port calling the target directly is only parity if it passes the same arguments;
where a thunk's constants are not obvious from the call site, they are recorded on the target's note.

That is 1952 bytes, 0.33% of the image. With them separated, the honest figure is
**968 behaviour-bearing functions, 968 cited, 0 uncited**. The coverage table in `PARITY.md`
now prints both numbers so the 1103 denominator cannot be read as 135 units of remaining work.

`python docs/parity/coverage.py --check` is the guard, and `tools/check.sh` runs it: every
artifact row must name a real inventory function with a known class and non-empty evidence; every
target address quoted in that evidence must itself be cited or be another artifact; and every
uncited function must have a row. A new uncited function without a row fails the gate instead of
quietly becoming part of the denominator.

Two things this classification corrected in the IDB itself. Six blocks the decompiler had named
after the nearest import (`?SelectGdiObject@CDC@@KAPAVCGdiObject@@...`, at 0x11148, 0x22604,
0x3f0b4, 0x460b4, 0x61498, 0x83880) never touch a device context: each is a CRT init-term entry
pairing a subsystem's constructor with its `atexit` registrar, reached through one pointer-table
slot. They are now `InitTerm_UiBankEntry`, `InitTerm_MapEntry`, `InitTerm_DatFileEntry`,
`InitTerm_FontEntry`, `InitTerm_BaseGridEntry`, `InitTerm_StrategListEntry`. And 0x9e0b0, held as
`nullsub_GeoscapeStub`, is the release build's compiled-out debug print — now `Debug_TracePrint`,
with its six call sites in `Tact_UpdateUnitControl` (0x4A5D4, 0x4A678), `BeforeEnemyStep` (0x4B9D8,
0x4C490) and `AfterEnemyStep` (0x4C4B0, 0x4C9BC); each one loads a label and `"s"` first, e.g.
0x4A5CC `"Enemy return from control"` and 0x4B9CC `"Before enemy step"`. `docs/idb/functions.tsv` was rewritten straight from the IDB
so the dump cannot lag behind a rename again; it disagreed on nine rows, seven of them these
renames and two (`Options_SetTab3_AndUiY @ 0x6b674`, `Geoscape_GameTickStub @ 0x94550`) from
renames made in earlier units.

The 135 artifact rows were re-derived from the IDB on 2026-10-22 instead of inherited: every row's name and
function size were read back live (`idc.get_name`, `ida_funcs.get_func`) and all 135 agree, 1952 bytes in both.
By instruction shape they are 85 import stubs (`LDR R12, =__imp_X; LDR PC, [R12]`), 26 thunks and tail calls,
8 `RET`-only nullsubs, 6 CRT init-term entries, 2 `B atexit` registrars, and 8 more that are CRT machinery
rather than game code: `start`, `_cinit`, `Crt_CallFnTable`, `Crt_DoExit`, `_onexit`, `atexit`, the compiled-out
debug print `Debug_TracePrint` @ 0x9E0B0, and the `POP {R0-R12,PC}` data word `nullsub_10` @ 0xA42E0 -- that
last one is also the only row in the image with no cross-reference at all, the other 134 all have one.
`docs/parity/coverage.py --check` is the structural guard (every row names a real inventory function with a
class and evidence, every target it cites is cited or another artifact); this paragraph is the instruction-level
confirmation under that guard.
