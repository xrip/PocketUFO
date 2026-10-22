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

Citation coverage is one claim; reach is another, and `docs/parity/teeth_ledger.py` now measures it over the same
968 behaviour-bearing rows. A row counts as reached when a test cites its original address, calls the original
name or its port owner directly (test text with the comments stripped, so a mention is not a call), or reaches it
through the port call graph from such a call. Measured after Part 119: 924 of 968 reached (896 by a direct call or
an address cite, 28 through the call graph), 44 cited but not reached -- and `docs/idb/ida_unreached_callees.py`,
run inside IDA over the live database, put all 44 in one class: their own callee chain lands on an address the port
owns and a test reaches (`MenuBtn_SetSpeed0 @ 0x18ecc` -> `GameSpeed_Set @ 0x260c0`), so nothing was unexplained
but nothing was claimed per address either. Part 120 turned that into per-address cites: 40 of the 44 are per-level
button handlers the port implements once, each family verified in the IDB by an instruction walk (members share one
body and differ only in the level immediate -- `MenuCtl8..14_OnClick` 11 instructions and 52 bytes each,
`SaveLoad_Slot0..6` 17 and 80, `BasePlaneScreen_SelectWeapon25..30` 13 and 68, `OptionsScreen_SetMusicVol0..5` 18
each with two of them 76 bytes rather than 92 and the same five steps), and the shared port routine now cites every
address it reproduces. `Smap_BeginCraftSelectBtn @ 0x84634` got a test instead (test_geoscape.c Case 6, since the
port installs it as a pointer and no call graph can follow that). Measured after Part 120: **965 of 968 reached**
(938 by a direct call or an address cite, 27 through the call graph), 3 cited but not reached -- `WinMain @ 0x9fcc4`,
`InitInstance @ 0x9fd48` and `PocketUFO_WndProc @ 0xa03ec`, the process entry points that only `tools/e2e.sh`
exercises, by running the real executable -- and residue 0 in the IDB classification. Nothing in any of that is an
assertion: a wrapper whose shared routine runs does
not have its own constant pinned, and the ledger says so in its own header. The same TSV measures that gap: of the
965 reached rows, 502 carry a test cite at their own address and 463 run under a test with no cite attributed to
them (`strat-ui` 121, `tact` 93, `strat` 90, `ui-core` 50, `render` 30, `save` 26, `app-shell` 19, `dat` 16,
`tact-ui` 12, `titles` 6; `util`, `platform`, `audio` and `boot-assets` are closed). The per-function
"check-fails-when-wrong" pass over those 463 is the part of the goal no gate runs, and PLAN row 121a carries it.
Measured after Part 123: cites at a row's own address **508**, without one **457**, with `titles` closed too
(`strat-ui` 121, `tact` 93, `strat` 90, `ui-core` 50, `render` 30, `save` 26, `app-shell` 19, `dat` 16, `tact-ui`
12 left). Measured after Part 124: **520** cited, **445** not, with `tact-ui` closed as well (`strat-ui` 121,
`tact` 93, `strat` 90, `ui-core` 50, `render` 30, `save` 26, `app-shell` 19, `dat` 16 left).
Measured after Part 125: **536** cited, **429** not, with `dat` closed too (`strat-ui` 121, `tact` 93,
`strat` 90, `ui-core` 50, `render` 30, `save` 26, `app-shell` 19 left). `pufo_dat.c` and `pufo_asset.c`
now cite each address at the function that reproduces it, not only in the whole-file `Evidence:` block
the ledger cannot attribute. The address-to-port-function map stayed at 604 of 968, and the reason is a
property of the ledger worth recording: `teeth_ledger.py` builds `port_function` with `p != name`, so a port
function that keeps the original's own name -- all sixteen of these do -- reads as a blank map cell even when
its address is cited on exactly the right definition. The map counts renamed port functions and nothing else.
A first draft of this Part put both accessor cites in one comment block and the ledger then claimed
`AssetReader_Size @ 0x9f964` was owned by `AssetReader_Found`, which sits above it: a shared comment
attributes every address in it to one function, so the two cites were split into two blocks.

Part 121 closed the 5 open `util` rows and learned a rule the ledger cannot enforce: a `@ 0x...` in a test file
counts whether it sits next to a check or in a header comment, so the first draft of `tests/test_rand.c` made
`InitInstance @ 0x9fd48` read as reached by naming it as the seeding site in its evidence comment. That cite is
removed -- no test reaches that function -- and reach went back to 965 with the same three entry points left. A
cite has to be earned by the checks it sits on.

Part 123 closed the 6 `titles` rows and produced the clearest rule of the pass so far: a range check is not a
pin on a random bound. `RandU16(100)` and `RandU16(101)` differ in exactly one output out of 32768, so the
"speed 1..4, amplitude 1..101" checks in `test_titles.c` could not tell one from the other and the mutations
`0x64 -> 0x65` and `3 -> 2` would have passed. The seeding loop at 0x46F1C..0x46F8C and the reborn at
0x46E2C..0x46E6C are pinned by exact values from a seeded generator instead (seeds 4242 and 777, the outputs put
through ((rand() % 32768) * n) / 32768 truncated, which is the arithmetic test_rand.c pins on its own). The
second rule is that some original constants are inert and no check can pin them: the credits window's `scroll >=
320` test and its 36-row span only ever decide about rows that are off screen, because a row is on screen exactly
when the window already includes it, and 36 is more than the 17 rows that fit. Those are written in the IDB note
instead of being pretended to be covered.

Part 124 closed the 12 `tact-ui` rows and found the second kind of toothless check: an expectation built out of
the constant under test. Nine `Inv_HandlePointer @ 0x12260` checks read `uTu == 20 - PUFO_INV_TU_POCKET_A`, which
moves with the constant, so changing `#define PUFO_INV_TU_POCKET_A 4` to 5 would have passed. They state 4, 5, 6,
7 and 8 as literals now. The filter row is the other shape worth naming: the original has seven 128-byte functions
that a byte census shows are identical except two immediates -- which pad global gets 1, and what lands in
`unk_1183DE` -- while the port implements the family once, so each address is cited at the shared routine together
with the value only it stores (0x17410 stores 0xFF, 0x17710 lights `pBank + 0x2AA`), and each has its own mutation.

Part 125 closed the 16 `dat` rows and produced two method findings. First: a mutation whose failure mode is a
crash reports as no teeth. `AssetReader_Read @ 0x9f8d0` with its bounds test removed looked toothless -- 0 failing
check lines -- because the over-read the test asked for was 400000 bytes into a 16-byte stack buffer, so the test
died before printing anything. The case is real, but it is only *measurable* when the wrongly-permitted read fits
the caller's buffer, which needed a member small enough to over-read by one byte (main.dat entry 41, `0.CTS`, 24
bytes). The same trap explains a second result: `DatFile_FlushWrite @ 0x9f234` patches the directory in the struct
at 21*nCurEntry - 8 and on disk at 21*nCurEntry - 4; the disk half failed 17 checks, the struct half failed none,
because every existing read reopened the file and re-read the directory from disk. Adding two checks on `dat.pDir[0]`
turned that case into 3 failing checks. A proof that reaches only one half of a two-half write is a half proof.
Second: some code is inert and the honest report is a proof of inertness, not a missing case. The read refill clamp
at 0x9F71C limits how many bytes a refill may pull past the chunk's end, and dropping it fails no check -- the copy
at 0x9F774 takes `nLeft` bytes from the buffer front and `nLeft` is already bounded by the chunk's remaining bytes,
so the bytes beyond what was needed are never consumed. That is written in the IDB note for 0x9f698, next to the
measurement that agrees with it.
