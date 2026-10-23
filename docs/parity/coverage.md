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

Part 126 closed the 19 `app-shell` rows and produced two tool findings worth keeping. First: the mutation runner
restored with a second whole-file `replace(new -> old)`, so a mutation whose replacement text also occurs somewhere
else restores too much. The `KillTimers_D @ 0x9e864` case shortened `KillOne(UIPULSE); KillOne(EXTRA); }` to
`KillOne(UIPULSE); }` -- which is also `Timer_KillBySpeed`'s tail -- and the restore put the dropped kill back in
BOTH functions, leaving a real edit in `pufo_timer.c` (0xC354 killed, where 0x9e8b4 kills 0xC350..0xC353) that only
`restored exactly: False` revealed. `mutate_run.py` now splices at the recorded offset, the source was repaired, and
all 30 cases were re-run with 30 exact restores. Second: a check that compares zero with zero has no teeth. The
first version of the `CDialogTemplate_GetCellByte @ 0x54c80` stride checks compared the accessor against the shipped
AVENGER.MA1 byte at the offset under test, and only 117 of that blob's 2400 slot bytes are non-zero, so three of
them were 0 against 0. They became a synthetic 2403-byte blob whose byte at every checked offset is different
(3->4, 43->44, 82->83, 763->252, 805->38, 2402->99). The third finding came from the registry rather than the
instructions: `CMP R3,#0xFF` + `BHI` at 0x9EE70/0x9EE74 rejects cbData strictly above 255, and `Reg_SetString`
stores strlen+1, so the longest value that reads back is 254 characters -- the port comment's claim that 256 stored
bytes are accepted was wrong, and the new test failed before the comment was corrected. Measured after Part 126:
**555** cited, **410** not (`strat-ui` 121, `tact` 93, `strat` 90, `ui-core` 50, `render` 30, `save` 26 left;
`app-shell` closed -- its three process entry points stay cited-but-unreached, exercised only by `tools/e2e.sh`).

Part 127 closed the 35 `save` rows and produced the rule that the whole save family needed: a DatFile chunk is a
byte stream, so a field's position is the position of its `DatFile_Write`/`DatFile_Read` call, not its place in the
struct. The port's write and read orders are symmetric, which makes a same-size reorder round-trip invisibly -- the
file stays self-consistent and simply stops matching what the original writes. About 150 `FileByteAt` checks in
`tests/test_save.c` now pin the byte the original's own call emits (research records at +9, +0x1F, +4, +0x14, +0,
+0xA, +8, +0x1E, +0x21, +0x19, +15, +6, +0x20, +2 over a 0x22 stride; smap sites written 740 of their 742-byte
stride, so site 199 lands at 20 + 199 * 740; the rmp level block last at 0x43F7C; the soldiers count read twice,
0x416D4 and 0x417D4). Teeth: 35 cases, one per row; the first sweep left 2 with no teeth and both were the same
mistake -- a pin on a byte that is not a pin on the field that byte is read into (`Save_ReadSmapSite`'s five
one-byte flags), and a pad gate whose boundary was never exercised (`nDay < 10` with fixture days 15 and 1). Seven
new checks later the re-run fails 2 to 42 checks in all 35 cases. Measured after Part 127: **581** cited, **387**
not (`strat-ui` 121, `tact` 93, `strat` 90, `ui-core` 50, `render` 30, `app-shell` 3 left; `save` closed). One cite
was removed on purpose: a comment that only contrasted `Save_ReadSmap` with `Smap_ZoomIn @ 0x81e44` handed that
`strat` row a cite it had not earned, and the ledger now counts it uncited again.

Part 128 closed the 30 `render` rows, and three of them were wrong in the port's code rather than missing checks:
`Blit_Indexed`'s 24bpp arm writes three bytes and advances two columns (0xA33A0 `ADD R7,R7,#2`), so the last byte
of a column is rewritten by the next one and the port had a clean 3-byte step; the keyed inner
`SoftSurf_BlitIndexedKeyed_Inner @ 0xa3638` reaches the mode > 2 arm, computes the row pointer and leaves through
the bare POP at 0xA3960, so a 24bpp keyed blit draws nothing at all, and the port drew pixels;
`SoftSurf_Blit_Inner @ 0xa47d0` sums six field terms into one accumulator with no clamp anywhere, so an
out-of-range alpha carries into the neighbouring field (511 over a zero destination gives 0xFFE1) where a clamped
rewrite gives 0x0001. All three were fixed in `src/pufo/render/pufo_render.c`, and the 93 frame pictures plus the
9 e2e pictures stayed byte-identical -- that is the measurement showing none of the three arms is on a shipped
path. Two addresses the ledger had mapped onto the 1:1 copy got their own faithful port: `Blit_CopyRows_HiRes
@ 0xa3bb8` (the 2x2 upscale, whose mode > 2 arm writes the SAME source byte three times at 0xA3D44/0xA3D60/0xA3D7C
and advances once at 0xA3D98) and `Render_BlitSurface_HiRes @ 0x9fa90`. `Blit_CopyRows @ 0xa3a14` is now the
original's do-while with the column counter re-seeded inside the row loop (`MOV R7,R5` at 0xA3A88); that shape
caught a real port bug, because a counter seeded once for all rows ran the test off the end of the surface. Two
rows went to `docs/parity/bugs.tsv` as stated deviations instead of fixes: `Palette_Load @ 0x3e47c` tests neither
reader result (0x3E49C..0x3E4B0 has no branch) and so stores 256 entries of whatever the stack held when the .pal
is missing, and its slot math is `& 0xFF` over 256-entry slots (0x3E568/0x3E56C/0x3E570) against the port's 64;
and `SoftSurf_BlitGlyph_Inner @ 0xa41d4`, whose penY < 0 arm spins without advancing (0xA42A4/0xA42A8) and whose
penY >= nSurf1 arm ends the glyph (0xA42BC to the POP at 0xA42DC). Teeth: 30 cases in
`docs/parity/mutations_part128.py`, one per row; the first sweep had 29 failing 1 to 130 checks and 1 inert -- the
`SoftSurf_ctor @ 0x9f96c` case, whose test only asserted that two other fields survived the ctor -- and after two
new accessor checks all 30 fail, 1 to 130 checks each, 245 failing checks in total, 0 skipped, every file back to
its exact bytes and `grep -rn "MUTATION" src tests` empty. Measured after Part 128: **611** cited, **357** not
(`strat-ui` 121, `tact` 93, `strat` 90, `ui-core` 50, `app-shell` 3 left; `render` closed).

Part 129 closes the `ui-core` family: the 50 rows row 121a still had uncited, and every one of them now
carries an `@ 0x...` cite sitting on a check that has teeth. The reads that made the cites honest are
instruction-level: `PadImage_FreeData @ 0x1100c` is four instructions and never writes 0 back to +0xC;
`PadImage_Load @ 0x11020` frees the old buffer at 0x1103C..0x11044 before the lookup, tests only
`AssetReader_Found` (ANDS/BEQ 0x11088/0x1108C), and reads the member's two little-endian u16 into +8/+0xA at
0x110BC..0x110CC with the mode byte store interleaved; `UiBank_CtorAll @ 0x11320` and `UiBank_FreeAllPads @
0x1117c` walk the same fourteen spans in opposite directions (first action of the release: widget 129) and
clear the two bank flags last at 0x114B8/0x114C8; `UiScrollList_ClickRow @ 0x3eee0` stores the row at
panel+0xC (0x3EF38) before its seven gates (0x3EF44..0x3EFCC) and only reads the cursor at 0xBD4;
`UiPanel_InsertLine @ 0x5a294` gates its top-down shift at 0x5A2A8/0x5A2AC, masks slot 0 at 0x5A2FC and
writes the count at 0x5A32C; `Ui_SwapStringSlots @ 0x6aaac` swaps strings at table+4*nIdx+3044 with aux
dwords at table+4*(nIdx+1261) through a 200-byte temp; `Font_Free @ 0x46144` hides its whole body behind
bInited (0x46154..0x4615C) and its slot loop behind nSlotCount, and clears only bInited (0x461D0);
`Font_GetSprByIndex @ 0x46600` has no bound at all; the selector pair `0x99f98`/`0x99fb4` picks the category
panel with an unbounded `LDRSB` of g_nUfopaediaCategory (0x99F9C/0x99FB8). Nine of these are recorded in
`docs/parity/bugs.tsv` as deviations rather than fixes (0x1100C, 0x11020, 0x1A2C8, 0x1A28C, 0x1A5C0,
0x46144, 0x46600, 0x3E8C8, 0x99F98) with the arm that would have to be reached named, and none of them is
reached by the shipped run. Teeth: 50 cases in `docs/parity/mutations_part129.py`, one per row -- 169
failing checks, 0 skipped, 0 inert, every file back to its exact bytes and `grep -rn "MUTATION" src tests`
empty; three first drafts were inert or mis-anchored and were re-cut (CtorAll now attacks the flag clear at
0x114C8, InsertLine the count byte at 0x5A32C, GetLineColor the slot read). Gates: `check: ALL SIX GATES
RAN` with exit 0, 65/65 tests, 21190 checks, 93 frame references and 9 e2e pictures unchanged. Measured
after Part 129: **661** cited, **307** not (`strat-ui` 121, `tact` 93, `strat` 90, `app-shell` 3 left; `ui-
core` closed).

Part 130 closes 40 `strat-ui` rows: `MenuBtn_SetSpeed0..4` (0x18ecc/0x18f20/0x18f74/0x18fc8/0x1901c),
`Options_SetMusicVol1..5` (0x19a10..0x19b60), `MenuBtn_SetSfxVol1..5` (0x19bb4..0x19d34),
`MenuBtn_ResizeWindow` (0x19930), `Menu_ToggleForceNoDrag` (0x19d94), the five plain confirm widgets
(0x196e8/0x19884/0x198a4/0x19910/0x1a014), `SaveLoad_Slot0..6` (0x19de4..0x19fc4), `MenuCtl0..3`
(0x1a09c/0x1a034/0x1a064/0x1a080) and `MenuCtl8..14` (0x1a0bc..0x1a1f4). Every family was diffed member by
member at instruction level: speed 19 insns + 2 pool words, music 18 + 3, sfx 21 + 3, confirm 7 + 1,
save/load 17 + 3, load 11 + 2, with the copies differing only in `MOV R1,#imm`. No port behaviour changed.
Two recorded claims were wrong and are fixed: the sfx family ends with Sound_Play(16,4) (0x19C00 is `MOV
R0,#0x10`), not 17 -- the 17 belongs to OptionsScreen_SetSfxVol0..5 at 0x6bc1c.. (0x6BC60 is `MOV R0,#0x11`)
-- and 0x19930 was recorded in the IDB as not ported although Menu_ResizeWindow over App_ResizeMainWindow is
wired through World_ResizeWindowSeamThunk (app/pufo_world.c:7290, widget 58). `Menu_ToggleForceNoDrag`
publishes the pad with the value its taken arm left in R1 (1 on the clear arm via 0x19DB8, 0 on the set arm
via 0x19DCC), an unconditional store at 0x19DD4. Six deviations went to `docs/parity/bugs.tsv` (364 -> 370
rows): the confirm five have no guard and hand the bank itself to Input_OnConfirm; the speed, music and sfx
bodies have no bounds test so the port's ValidStep is extra (dropping it from Menu_SetSpeed changed no check
-- GameSpeed_Set bounds the level); ToggleForceNoDrag always writes the pad; MenuCtl8..14 have no 'no
callee' arm. Teeth: 40 cases in `docs/parity/mutations_part130.py`, one per row -- 247 failing checks, 0
skipped, 0 inert, every file back to its exact bytes and `grep -rn "MUTATION" src tests` empty; nine first
drafts were re-cut (six broke the build through the unreferenced-parameter warning at /WX, three were inert,
one of them because the old 'mode is 3 while the load runs' check read the mode only after the call, which
is why tests/test_menubtn.c now reads it inside the load hook, 220 -> 227 checks). Gates: `check: ALL SIX
GATES RAN` with exit 0, 65/65 tests, 21233 checks, 93 frame references and 9 e2e pictures unchanged.
Measured after Part 130: **701** cited, **267** not (`tact` 93, `strat` 90, `strat-ui` 81, `app-shell` 3
left).

Part 131 closes 29 `strat-ui` rows on the options screen: `OptionsScreen_SetMusicVol1..5`
(0x6ba70/0x6babc/0x6bb18/0x6bb74/0x6bbd0), `OptionsScreen_SetSfxVol0..5` (0x6bc1c..0x6be10),
`OptionsScreen_ClearMusicButtons` 0x6b994, `ClearSfxButtons` 0x6b9d4, `OptionsScreen_PanelPrev` 0x6d01c,
`PanelNext` 0x6d064, `OpenWithSound` 0x6dd04, `SaveGame_Slot1..6_CloseUI` (0x6afcc..0x6b148),
`LoadGame_Slot1..6_InitOptions` (0x6b1fc..0x6b404) and `Ufopaedia_CacheSelRow` 0x99fd0. One port behaviour
changed, and it was a parity bug: all six sfx handlers end with sample 17 played TWICE - 0x6BC5C/0x6BC60
`MOV R1,#4`/`MOV R0,#0x11`, 0x6BC64 `BL Sound_Play`, then the same pair again at 0x6BC68/0x6BC6C, POP at
0x6BC70 and the tail `B Sound_Play` at 0x6BC74 (0x6BC5C..0x6BC74 is E3A01004 E3A00011 EB00C915 E3A01004
E3A00011 E8BD4000 EA00C911 in every member) - while the port played it once and the IDB note at 0x6bc1c
described one tail call. `OptionsScreen_SetSfxVol` now plays twice and `tests/test_baseui.c` counts it: 0
plays at step 0 (Sound_Play @ 0x9E10C tests Map_bSoundOff first) and 2 at every other step; the music family
has no Sound_Play at all. A second finding was in the test: the store at 0x6B1C4 (`gOptionsScratch+4`) and
the byte OptionsScreen_Init branches on at 0x6BE84 are one byte, so a load click forces the teardown branch,
but `TestSaveLoadSlots` wired the slot env to a separate cell, which made the check non-causal and a
first-draft mutation inert; the fixture now uses the one cell (as app/pufo_world.c:1728/:9084 do) and that
mutation fails 17 checks, with 14 cites added (test_baseui runs 4317 checks). Records re-measured member by
member: sfx 100 B = 23 insns (2 two-word MOVs) + 2 pool; music 92/76/92/92/92/76 = 20/17/20/20/20/17 insns +
2 pool; save 76 = 15 + 4; load 104 = 21 + 5; clears 64 = 15 + 1; panels 72 = 14 + 4; opener 32 = 8 insns;
cache 68 = 14 + 3 - the wide 2-word MOV immediates are why 4*insn count is not the byte size. Eight shapes
went to `docs/parity/bugs.tsv` (351 -> 359 data rows): `Options_ClampStep` is invented and measured inert
when removed; the clears and the panel pair dereference every slot unguarded where the port guards them;
save/load have no guard at all so the port's NULL arms are invented, and load forces the teardown;
CacheSelRow returns R0 (an address on one path, a flag byte on the other) where the port returns a bool and
has no direct call site (it is installed as a click handler at 0x9A958); OpenWithSound drops its first
argument. Teeth: 29 cases in `docs/parity/mutations_part131.py`, one per row - 236 failing checks, 0
skipped, 0 inert, all files restored byte-exact, MUTATION grep empty. Gates: `check: ALL SIX GATES RAN`,
`CHECK EXIT=0`, 65/65 tests both configurations, 21372 checks, 93 frame references and 9 e2e pictures
unchanged; coverage unchanged at 972/1103 functions and 596888/598792 bytes. IDB comments on 32 addresses
(the 29 rows plus heads 0x6ba14/0x6bc1c/0x6af80/0x6b194), longest 971, all read back exact, two stale claims
replaced, `idb_save` ok. Measured after Part 131: **730** cited, **238** not (`tact` 93, `strat` 90,
`strat-ui` 52, `app-shell` 3 left).
## Part 132 - 28 `strat-ui` rows: nine close thunks, seven panel pairs, five weapon copies

Closed this Part: `BaseInfoScreen_CloseThunk` 0x557bc, `BaseFacScreen_CloseThunkA` 0x589c0,
`BaseFacScreen_CloseThunk` 0x59b54, `SoldierScreen_CloseThunk` 0x64ae4, `ManufScreen_CloseThunk` 0x6e3d4,
`ResearchScreen_CloseThunk` 0x72ffc, `BuySellScreen_CloseThunk` 0x75154, `BaseAtScreen_CloseThunk` 0x577c0,
`NewBaseScreen_CloseThunk` 0x615b0; the panel pairs 0x589cc/0x589dc, 0x6dd4c/0x6dd94, 0x715e8/0x71648,
0x72e30/0x72e68, 0x79a20/0x79a48, 0x79a70/0x79ac8, 0x967e4/0x9683c; and `SelectWeapon26..30`
0x79b64/0x79ba8/0x79bec/0x79c30/0x79c74.

New cites: 54 in `tests/test_baseui.c` (8 close-thunk, 46 panel-pair) (the seven-member thunk loop with four checks each, the new BaseFac floor
step, the research ceiling step, the crew/detail/psi/manuf pair cites) and 14 in `tests/test_geoscapeframe.c` (the
transfer nav block: five cursors per press, the floor, the next page, the `nLineCount - nStyle` stop; and the
weapon-wiring block at the end of the run: five mounts x three asserts). Total test checks 21372 -> 21450.

Records re-measured item by item, code separated from pool: 12 B = 2 + 1 for each of the twelve 12-byte close
thunks, 72 B = 14 + 4 at 0x577c0, 32 B = 7 + 1 at 0x615b0, 16 = 3 + 1, 40 = 8 + 2, 56 = 11 + 3, 72 = 14 + 4,
96 = 19 + 5, 88 = 17 + 5 for the panel pairs, 68 = 13 + 4 for each weapon copy. No two-word MOV immediate occurs in
any of these 28 records, so here 4 * insn count + 4 * pool count is the size exactly. Six claims the tree carried
were wrong (five sizes and one "byte-identical"); all six are now `docs/parity/bugs.tsv` rows (359 -> 365 data
rows) and the IDB comments at the 28 addresses carry the measured shape.

Family facts worth keeping: 25 bodies end `B Geoscape_CloseUI` @ 0x8b94c and 12 of them are the 12-byte shape,
differing only in the branch-displacement word; the three 88-byte panel bodies (0x76088/0x760e0, 0x79a70/0x79ac8,
0x967e4/0x9683c) share their 17 instructions and their 5 pool words, and differ only in 5 or 10 words of branch
displacement plus pool; consecutive weapon copies differ in 3 words.

Teeth: 28 cases in `docs/parity/mutations_part132.py`, one per row - 113 failing checks, 0 skipped, 0 inert, every
file restored to its exact bytes, `grep -rn "MUTATION" src tests` empty. `docs/parity/mutate_run.py` got one fix:
the child output is decoded as UTF-8 with `errors="replace"` instead of the host locale codec, which had been
swallowing a decode error in `test_geoscapeframe`'s caption output and handing back `stdout = None`.

Gates: `check: ALL SIX GATES RAN`, `CHECK EXIT=0`, 65/65 tests in both configurations, 21450 checks, coverage
unchanged at 972/1103 functions (88.1%) and 596888/598792 bytes (99.7%), 135 artifact rows, 131 uncited functions,
0 real gaps, 93 frame pictures and 9 e2e pictures unchanged. IDB comments on 28 addresses, all read back exact
(longest 999), `idb_save` ok.

Measured after Part 132: **758** cited, **210** not (`tact` 93, `strat` 90, `strat-ui` 24, `app-shell` 3 left).

## Part 133 -- the last 24 strat-ui rows closed, 186 left

All 24 remaining `strat-ui` rows now carry a cite at their own address (`strat-ui` 250/250). Cite totals from
`docs/parity/teeth_ledger.py`: 968 behaviour rows, 782 cited, 186 not -- `tact` 93, `strat` 90, `app-shell` 3.

No port behaviour changed. Every record was re-measured item by item (code items vs pool words, `4*items + pool ==
size` asserted): 44 = 8+3 (0x557c8, 0x716a8), 40 = 7(1 wide)+2 (0x6dd24, 0x76138, 0x96ab0), 88 = 22+0 (0x96ad8),
68 = 15+2 (0x588cc), 3872 = 925(25)+18 (0x57808), 420 = 94(1)+10 (0x58728), 2404 = 571(14)+16 (0x6e9e8), 488 =
102(6)+14 (0x710b0), 2940 = 700(17)+18 (0x716d4), 848 = 198(2)+12 (0x71298), 328 = 75(2)+5 (0x72250), 332 = 74+9
(0x72b40), 4236 = 1000(34)+25 (0x76de0), 772 = 180(3)+10 (0x75160), 784 = 181(5)+10 (0x75878), 3112 = 737(18)+23
(0x76160), 420 = 86(6)+13 (0x9690c), 1372 = 325(4)+14 (0x96288), 604 = 140(2)+9 (0x9602c), 3956 = 931(37)+21
(0x96b30), 728 = 169(6)+7 (0x79748). `0x76d88` vs `0x96ad8`: 0 of 22 words differ, byte-identical.

Two port claims corrected against the record (bugs.tsv 365 -> 367, kind `naming`): the defence burn is a fixed
immediate per pair (500 at 0x57CE0, 600 at 0x57E8C, 900 at 0x5803C, 1500 as 0x5D0|0xC at 0x581E0 with the subtract
at 0x581EC; the gravity pair burns nothing -- 0x58390 is a label-copy pointer difference) rather than the value the `/* 0x57CE4 */` comment
implies; and 0x76C18 is the setup `ADD R3,R0,#2` of the teardown's 256 x `STRH [R3],#2` clear at 0x76C20, which
spans exactly `SmapSite.auItemQty` at 0x002..0x201. One anchor had no teeth at all (nothing read the last slot of the clear); the burn anchor was inert because of its
direction, not because nothing reads the budget -- `TestBaseAtUpdate` already pins the burns with hit fixtures.
Part 134 closed both halves: 8 interval checks on the burns, 3 last-slot checks on the clears (PLAN row 133a).

Tests: 174 new address cites (167 in `test_baseui.c`, 7 in `test_plane.c`), check count unchanged at 21450. 14 of
them went onto the specific checks the mutations break, so each failing line names its own row.

Teeth: 25 cases in `docs/parity/mutations_part133.py` -- 45 failing checks, 0 skipped, 0 inert, 0 build failures,
every file back to its exact bytes. 34 of the 45 failing checks name the row address and every row has at least one
that does. Log `build/crops/teeth133.log`.

IDB: 16 head comments (12 new) and 4 inner notes at 0x6EA7C / 0x57914 / 0x9628C / 0x7974C, all read back exact
(longest 1018); four heads that had been cut off at the 1024-character comment limit now end at a complete sentence.
`idb_save` ok.

Gates: `build/check133.log` -- `check: ALL SIX GATES RAN`, `CHECK EXIT=0`, 65/65 both configurations, 21450 checks,
coverage unchanged at 972/1103 functions and 596888/598792 bytes, 135 artifact rows, 131 uncited functions, 0 real
gaps, 93 frames and 9 e2e pictures unchanged.

Measured after Part 133: **782** cited, **186** not (`tact` 93, `strat` 90, `app-shell` 3 left).
## Part 134 -- row 133a closed: burn intervals and the last slot of each teardown clear

No port behaviour changed; 17 checks added (21450 -> 21467), 17 cites, 12 mutation cases.

Two of Part 133's statements were wrong and are corrected in `docs/parity/bugs.tsv`, `docs/parity/coverage.md`,
`docs/parity/game-core.md`, PLAN.md and the IDB. (1) `SUB R3,R3,R1` at 0x58390 is not a burn -- it is the pointer
difference that opens the "ALL RECHARGED" label copy, and the gravity pair stores nothing to the budget cell.
(2) "no fixture in `TestBaseAtUpdate` rolls a HIT" is false -- tests/test_baseui.c:17689..17716 pins all four burns
with hit fixtures and 17717..17742 the eight-class budget table; Part 133's `+1` mutation was inert because of its
direction (every boundary there is `reaches exactly 0`, and 501 off 500 is still below 1).

What is true, and what the new checks are for: the budget cell [SP,#var_C0] is never written out of the frame, its
only read is the stop test, so a burn is observable as a pair count -- an interval. The suite now pins each constant's
two ends: missile 500..599 (`SUB #0x1F4` 0x57CE0), laser 600..749 (`#0x258` 0x57E8C), plasma 750..999 (`#0x384`
0x5803C), fusion 1500..2199 (`0x5D0|0xC` 0x581E0, subtract 0x581EC). The clear half was a real gap: `auItemQty[255]`,
`anSoldierIds[99]` and `anCraftIds[9]` are prefilled and asserted, and `bUnk518` (site+0x206) is asserted to survive
the teardown, which the port only claimed in a comment.

Teeth: 12 cases in `docs/parity/mutations_part134.py` -- 22 failing checks, 0 skipped, 0 inert, 0 build failures, 12/12
files restored byte-exact, every case's failing check naming 0x57808 or 0x76160. Log `build/crops/teeth134.log`.

IDB: 0x57914 corrected, 0x57CE0 carries the burn census and the interval statement (555 chars), both read back exact,
`idb_save` ok. Gates: `build/check134.log` -- `check: ALL SIX GATES RAN`, `CHECK EXIT=0`, 65/65 both configurations,
21467 checks, 972/1103 functions and 596888/598792 bytes, 135 artifact rows, 131 uncited functions, 0 real gaps, 93
frames and 9 e2e pictures unchanged.

Measured after Part 134: **782** cited, **186** not (`tact` 93, `strat` 90, `app-shell` 3 left) -- this Part closed a
PLAN row, not a ledger row.

## Part 135 -- InitInstance's startup block ported: 783 cited / 185 uncited

The port ran `srand` and stopped; the original spends a warm-up (`rand()` `wSecond` times,
0xA0108..0xA011C) and nine `Game_RandU16` draws (0xA0134..0xA024C) in front of the asset paths. Two of the
nine results are live -- `Map_nMissionType` @ 0x128AD2 (drawn 0..9 from a bound of 10, redrawn while 7) and
`Map+0x1511E` @ unk_128AD6 (raised when a 0..1 draw is 0, cleared again for mission types 3 and 4) -- and
both were 0 in every new game. The other seven cells (0xAC6C0, 0xAC6C4, 0xAC6C8, 0xAC6CC, 0x188D58,
0x188D5C, 0x188D60) have no reader in the image -- measured as direct xrefs and again as a
base-displacement scan (103 functions touching their neighbours, 12 991 instructions, hidden reach 0) -- but
they are still draws, and the draws decide every roll that follows.

`Game_SeedRng` now spends the warm-up, `Game_SeedRngFixed` seeds from the shell's clock pair (30 s 0 ms ->
seed 3000 + 30 draws) instead of a fixed seed, `Game_SeedStartupPicks` reproduces 0xA0134..0xA024C in order,
and `wWinMain` installs the two live cells after `World_Init` (which memsets them). `LoadAcceleratorsW` asks
for resource 3 (`MOV R1,#3` @ 0x9FCE4).

11 new checks (test_rand 7 -> 18), all citing `@ 0x9fd48`; a new `tools/e2e.sh` state assertion proves the
install reaches the live world (`mission type 6, map 0x1511E 0`). Teeth: 8 mutation cases, 22 failing checks,
0 inert, 0 skipped, 8/8 restored -- plus a picture proof: dropping the 0xA0214 draw runs the flow at mission
type 9 and differs from the committed `01_newgame.png` in 53 595 of 76 800 pixels.

Pictures: 93 frame references unchanged; one e2e reference re-referenced -- `01_newgame.png`, 414 of
76 800 pixels, x 8..78 / y 203..226, the two coordinate readouts only (868.845.861 / 748.608.437 ->
500.782.175 / 560.926.480). The other 8 e2e pictures are identical.

IDB: InitInstance head + 6 block notes, the seven write-only cells, the reader/writer census at 0x128AD2 and
0x128AD6 (including the save pair's pointer-passed writes 0x4430C/0x44318, 0x44334/0x44340), 0xA0250's note
restored after it was found missing, and 0xA02F8's operand comment corrected (the literals are plain
`lang.dat` / `main.dat` 20 bytes apart). All read back exact, `idb_save` ok. Gates: `build/check135b.log` --
coverage: 972/1103 functions (88.1%), 596888/598792 bytes (99.7%), tests: 65/65 passing, check: OK -- 135 artifact rows, 131 uncited functions, 0 of them real gaps, 21478 checks, frames: unchanged (93 reference pictures) and e2e pictures: unchanged against the committed references (9 pictures) unchanged.

Measured after Part 135: **783** cited, **185** not (`tact` 93, `strat` 90, `app-shell` 2 left -- the two
`app-shell` rows are `WinMain @ 0x9fcc4` and `PocketUFO_WndProc @ 0xa03ec`, to be closed through the e2e gate).

## Part 136 -- the app-shell rows close: 785 cited / 183 uncited

`app-shell` is closed. `pufo_app.c` became the static library `pufo_app`, the executable kept only
`app/pufo_main.c` (four lines, `wWinMain` -> `App_Run`), and `tests/test_appshell.c` calls the pump and the window
procedure themselves. The two rows read `test_cites_addr yes`, `test_names_it yes`, `test_calls_directly yes`, with
`App_PumpMessages|App_Run` as WinMain's port function.

What the record has and the port did not: the WM_COMMAND arm at 0xA0C9C..0xA0CDC -- wParam's low half tested
against 1, then `SendMessageW WM_ACTIVATE` with the hwnd in the lParam slot (0xA0CB0) and `SendMessageW WM_CLOSE`
(0xA0CC4), answering 0; and the pump's order, with `TranslateAcceleratorW` first at 0x9FD0C and the exit code taken
from `Msg.wParam` at 0x9FD44. `!= 0` at 0x9FD38 replaces the port's `> 0`; that difference has no teeth (GetMessageW
never answered negative in this suite) and is recorded instead of faked.

27 new checks (21478 -> 21505 total). Teeth: 9 cases, 25 failing checks, 0 inert, 0 skipped, all
restored byte-exact. No picture proof for the dispatch: the scripted dumps run the timer bodies through
`App_ScriptTicks`, not through `PocketUFO_WndProc`, so the 9 e2e pictures and the 93 frame references are unchanged
and the unit mutations carry these rows' teeth.

Four `deviation` rows went into `docs/parity/bugs.tsv`: WM_PAINT presents where 0xA0444..0xA0468 draws nothing;
WM_SIZE (5) re-presents where 0xA041C/0xA0420 swallows the message; WM_ERASEBKGND answers 1 where 0x14 falls to
0xA0740's DefWindowProcW; App_MapPointer inverts the shell's letterbox instead of the DisplayMode table at
0xA0768..0xA07E0. IDB: 0x9fcc4 (1015), 0xa03ec (972, WM_SETFOCUS -> WM_CLOSE), 0xA0C9C (951), 0xA0768 (873),
0xA0810 (272), 0xAD710 (902 with its two-stores-only census). All read back exact, `idb_save` ok.

Gates: coverage: 972/1103 functions (88.1%), 596888/598792 bytes (99.7%), tests: 66/66 passing, check: OK -- 135 artifact rows, 131 uncited functions, 0 of them real gaps, 21505 checks, frames: unchanged (93 reference pictures), e2e pictures: unchanged against the committed references (9 pictures).

Measured after Part 136: **785** cited, **183** not (`tact` 93, `strat` 90); `app-shell` 24/24 cited.

## Part 137 -- 28 `strat` rows close: 813 cited / 155 uncited, and the manuf count fixed at 144

`Base_CountActiveManuf @ 0x68264` reads its entry count from the byte at 0x160C4C (144, written once at 0x859E4),
not from an argument, and sums that many u16 of a 200-entry-per-base column. The port hoists the call into
`Base_UpdateWorkshopNeeded`'s callers and four of them passed 200: `pufo_geoscape.c` (0x62920), `pufo_baseui.c`
(0x8BA64), `pufo_save.c` (0x458B8), `pufo_geoiinit.c` (0x953E4). Products 144..148 exist (149 object records), so the
port charged rows the original cannot count to the workshop budget. All four now pass the shared byte. The
`Game_StartNew` site is inert by construction -- 0x947EC..0x94814 clears `auManufCol` before 0x953A4's passes.

4 new checks in `TestManufCountIsTheSharedByte`: row 143 counts, row 144 does not, and the 0x62920 pass raises the
needed flag for one and not the other. 28 rows gained test citations, so the ledger moved 785 -> 813 cited
(`tact` 93, `strat` 62 left). Teeth: 27 cases, 121 failing checks, 0 inert, 27/27 restored byte-exact. Two inert
cases (the lab and stores budget divisors) were replaced by cell-kind mutations after measuring that both divisors
give the same budget at the fixtures' totals.

coverage: 972/1103 functions (88.1%), 596888/598792 bytes (99.7%), tests: 66/66 passing, check: OK -- 135 artifact rows, 131 uncited functions, 0 of them real gaps, 21509 checks, frames: unchanged (93 reference pictures), e2e pictures: unchanged against the committed references (9 pictures). IDB: 0x68264 (789), 0x68278 (189), 0x62920 (377), 0x8BA64 (202), 0x458B8 (207), 0x953E4 (311); `idb_save` ok.

Measured after Part 137: **813** cited, **155** not (`tact` 93, `strat` 62).

coverage: 972/1103 functions (88.1%), 596888/598792 bytes (99.7%), tests: 66/66 passing, check: OK -- 135 artifact rows, 131 uncited functions, 0 of them real gaps, 21513 checks, frames: unchanged (93 reference pictures), e2e pictures: unchanged against the committed references (9 pictures). IDB: 0x83890 (523), 0x821a0 (412), 0x838ac (380+133); `idb_save` ok.

Measured after Part 138: **836** cited, **132** not (`tact` 93, `strat` 39).
coverage: 972/1103 functions (88.1%), 596888/598792 bytes (99.7%), tests: 66/66 passing, check: OK -- 135 artifact rows, 131 uncited functions, 0 of them real gaps, 21523 checks, frames: unchanged (93 reference pictures), e2e pictures: unchanged against the committed references (9 pictures). IDB: no new note this Part -- the census found a comment at all 20 batch addresses; `idb_save` not needed.

Measured after Part 139: **856** cited, **112** not (`tact` 93, `strat` 19).
coverage: 972/1103 functions (88.1%), 596888/598792 bytes (99.7%), tests: 66/66 passing, check: OK -- 135 artifact rows, 131 uncited functions, 0 of them real gaps, 21526 checks, frames: unchanged (93 reference pictures), e2e pictures: unchanged against the committed references (9 pictures). IDB: 0x5a830 extended (456); `idb_save` ok.

Measured after Part 140: **866** cited, **102** not (`tact` 93, `strat` 9).
coverage: 972/1103 functions (88.1%), 596888/598792 bytes (99.7%), tests: 66/66 passing, check: OK -- 135 artifact rows, 131 uncited functions, 0 of them real gaps, 21526 checks, frames: unchanged (93 reference pictures), e2e pictures: unchanged against the committed references (9 pictures). IDB: no new note (census found a comment at all 9 batch addresses).

Measured after Part 141: **875** cited, **93** not (`tact` 93).
coverage: 972/1103 functions (88.1%), 596888/598792 bytes (99.7%), 66/66 tests passing, check: OK -- 135 artifact rows, 131 uncited functions, 0 of them real gaps, 21564 checks, frames: unchanged (93 reference pictures), e2e pictures: unchanged against the committed references (9 pictures). IDB: 27 function comments written and read back exact (the tact click-table handlers this Part pressed). Bugs ledger: +1 deviation row (Tact_TryFireMode4/5/10 push record 6 where the original pushes record 1).

Measured after Part 142: **907** cited, **61** not (`tact` 61).
coverage: 972/1103 functions (88.1%), 596888/598792 bytes (99.7%), tests: 66/66 passing, check: OK, 21567 checks, frames: unchanged (93 reference pictures), e2e pictures: unchanged against the committed references (9 pictures). IDB: 7 MapCell function comments written and read back exact (0x5487c, 0x548a0, 0x548c0, 0x548d0, 0x54a90, 0x54b14, 0x54b58), the IDB saved. No new bugs.tsv row: the only defect found is a wrong comment in the port (MapCell_ListRemove's tail case), not a behaviour gap.

Measured after Part 143: **914** cited, **54** not (`tact` 54).
coverage: 972/1103 functions (88.1%), 596888/598792 bytes (99.7%), tests: 66/66 passing, check: OK, 21567 checks, frames: unchanged (93 reference pictures), e2e pictures: unchanged against the committed references (9 pictures). IDB: 8 tactstate function comments written and read back exact (0x49b84, 0x49d5c, 0x4a37c, 0x4a758, 0x4f300, 0x4a3d4, 0x4f2d4, 0x4b2f4), IDB saved. Bugs ledger: +1 deviation row (the port's queue-capacity guard in TactState_EnqueueThrow and its sibling, which the original does not have).

Measured after Part 144: **922** cited, **46** not (`tact` 46).
coverage: 972/1103 functions (88.1%), 596888/598792 bytes (99.7%), tests: 66/66 passing, check: OK, 21567 checks, frames: unchanged (93 reference pictures), e2e pictures: unchanged against the committed references (9 pictures). IDB: 9 function comments written and read back exact (0x260c0, 0x26f88, 0x26fa8, 0x27324, 0x3242c, 0x3a2ac, 0x49410, 0x49eb8, 0x4d950), IDB saved. No new bugs.tsv row this Part.

Measured after Part 145: **931** cited, **37** not (`tact` 37).
coverage: 972/1103 functions (88.1%), 596888/598792 bytes (99.7%), tests: 66/66 passing, check: OK, 21567 checks, frames: unchanged (93 reference pictures), e2e pictures: unchanged against the committed references (9 pictures). IDB: 15 function comments appended (pin lines only, no existing analysis overwritten) and read back, IDB saved. No new bugs.tsv row.

Measured after Part 146: **946** cited, **22** not (`tact` 22).
coverage after re-derivation (Part 147, PLAN 119a): 972/1103 functions (88.1%) cited by src comments, 131 uncited functions carry a no-behaviour artifact row (1904 bytes, 99.7% cited), 0 real gaps. `coverage.py --check` now reads: `check: OK -- 135 artifact rows, 131 uncited functions, 0 of them real gaps`, with 4 notes for rows that src also cites (0x11148, 0x18a44, 0x22b50, 0xa115c). The artifact ledger's header records the re-derivation (2026-10-23) and the `atexit` row's evidence names its six *RegisterAtexit callers.

Behaviour-row ledger: 968 rows, 946 cited, 22 uncited (all `tact`) -- that is the remaining coverage gap, not the function ledger.
coverage after the start-to-end chain (Part 148, PLAN 120b): the function ledger is closed at 1103/1103 -- 972
functions cited by `src/` comments, and the 131 uncited ones each carrying a no-behaviour artifact row proven
from the IDB in Part 147: 84 import thunks (library imports), 26 code thunks, 7 empty stubs, 5 init-term
entries, 7 CRT runtime entries, 1 debug-print stub that drops all four arguments, 1 decode artifact. Under the
gate rule the user set on 2026-10-23 (functions proven to be library or WinCE-specific may be left out of the
100% coverage gate), the coverage gate is closed by that proof rather than by citing them. What is left is
behaviour teeth, not coverage: 946 of 968 behaviour rows carry a test citation, 22 do not, all 22 cited in
`src/`, 16 with a port function named in the ledger cell and 6 with that cell empty. Start-to-end: `tools/e2e.sh` now asserts the save->load cross-process
state match (`1 354/86 8 4147000 6 0` on both sides) and quit-YES over a loaded world (8 live items -> 0,
mirror 0x186530 unchanged at 5), each with its own mutation record in build/crops/mut148.log. Gates: 66/66
tests, 21567 checks, 93 frames and 9 e2e pictures unchanged, check: ALL SIX GATES RAN, CHECK EXIT=0
(build/check148.log). No src/tests change, no IDB change (the -6000/-6000 unplaced-base sentinel the chain
shows was already recorded at 0x94964 in `pufo_geoiinit.h` item 11).

coverage (Part 149): two signed-off src fixes. Ui_TryFireMode's refusal now pushes text record 1 as 0x179e4 /
0x17a80 / 0x17b1c all do at 0x17A54 (StatusMsg_Push @ 0x2DDA4 with aTextRec1_OutOfTime @ 0xB399D); the check that
was supposed to cover it counted status pushes only, so tests/test_tactui.c now asserts the pushed index
(g_log.aStatus[0]). MapCell_ListRemove's comment claimed the tail link is left dangling; 0x54b58 clears it, the
code already did, and a mutation that makes the code match the old comment fails 2 checks. Function coverage is
unchanged (972/1103 cited, 131 proven artifacts); the behaviour ledger is unchanged (968 rows, 946 cited, 22
uncited) because this Part corrected a wrong value rather than closing a row. Teeth: 2 mutation cases, 5 FAIL
lines, 0 inert, both restored byte-exact (docs/parity/mutations_part149.py, build/crops/mut149.log). IDB: the
"PORT DEVIATION" sentence replaced in 0x179e4/0x17a80/0x17b1c (947/770/766 chars), read back exact, IDB saved.
bugs.tsv row 395 records the fix (373 rows). Gates: 66/66 tests, 21568 checks, 93 frames and 9 e2e pictures
unchanged, check: ALL SIX GATES RAN (build/check149.log, build/check149b.log).

coverage (Part 150, PLAN 122): the start-to-end chain now includes a live tactical mission brought up by the
executable itself -- 0x83ABC GeoscapeUi_FreeAll mode 0 (clearing Map_bGeoscapeUi at 0x86D34, which opens
GameTick_OnTimer's tactical branch at 0x27508) then 0x83AC4 Tact_InitMission mode 0, dispatched through the world's
own mission-seam hooks. Live and asserted: 0x83AC4 returns 1, 8 of the world's soldiers deployed, tactical ticks
live, pen press through Input_OnPointer's tact arm, quit-YES closing a live mission ("no live mission", exit 0), and
one new committed picture (tests/ref/e2e/13_tact_unit_panel.png, the unit panel drawn with lang.dat strings). Named
shortfall, not hidden: the mission has no enemy roster and the queue stays empty because the tier/variant cells come
from Smap_BeginCraftSelect @ 0x838dc's tree (selection reads the root line list at 0x839CC/0x839E4) and a scripted
run has no contact line -- PLAN 122a. Teeth: 2 flow mutations, each its gate's only failure line, each restored
byte-exact (build/crops/mut150.log). IDB: 0x3c550's Map_nUiScreen arms appended to its comment (1004 chars, read
back), IDB saved. Function and behaviour ledgers unchanged (972/1103 cited, 131 proven artifacts; 968 rows / 946
cited / 22 uncited). Gates: 66/66 tests, 21568 checks, 93 frames, 10 e2e pictures unchanged, check: ALL SIX GATES
RAN (build/check150.log; build/e2e150.log for the first run of the new reference).

Measured after Part 151: **968** cited, **0** not -- the behaviour ledger is closed at 968/968 rows.
coverage (Part 151): the last 22 `tact` behaviour rows are cited on the checks that already assert them, and
each of the 22 has a mutation that fails its own port function (24 cases, 0 inert, 24/24 byte-exact restores).
One cite had to move: `Path_EnqueueCell @ 0x2b534` sat on a `Unit_PathToCell` queue check, so `tests/test_path.c`
gained `TestEnqueueCell` (10 checks) for the frontier lists the original owns. The function ledger is unchanged
from Part 147: 1103 = 972 cited + 131 proven no-behaviour artifacts, 0 real gaps.
Measured after Part 152: still **968** cited, **0** not -- regenerating the ledger after the owner fix changes
no reach number. The `void` owner PLAN 151 reported is gone (76 rows re-mapped to their click handlers, 0 rows
lost a mapping, empty port cells still 363).
coverage (Part 152): PLAN 123a closed in docs/parity/teeth_ledger.py (a keyword is never an owner; a cite on a
function-pointer table entry belongs to the handler named on that line). PLAN 122a's gate measured on both
sides: the flag-23 mission line comes from Strateg_ConfirmInterceptTarget @ 0x7fb98, and reaching it needs a
detected contact -- Strateg_CalcDetect @ 0x81378's site-state arms (1/2/11/12), its 24/31 radius, the
`nTimer == 0` test and `Game_RandU16(1000) < empty-cell-count(+7)`. Live measurement: 2000 game-minutes,
closest approach 25.3 tiles, 0 detections, 0 lines.
coverage (Part 153): PLAN 122a's first link measured live, no src change. A placed new-game base has site state
1 (one free kind-4 cell) and a detection pct of 2+7 = 9 per thousand -- anBaseRadarPct is Base_CountEmptyFacilityCells
(0x9461C, 0x4582C), not a radar flag. docs/parity/measure_122a_sweep.sh: 60000 pulse steps = 5955 game-minutes gave
one detected contact (line `flag 10 slots 4`, 0x818F4's contact message) at 18.4 tiles from the base; 15000 pulses
(1500 min) gave none. The chain from a visible contact to a mission is pen-driven and every command exists already.
coverage (Part 154): `--script fasttick n` added (user-approved) -- the three timer bodies without the paint,
proven a pure accelerator (identical 40 clock readings, identical item counts, same contact at 357300 s). New
measured fact for PLAN 122a: the contact message pauses the geoscape clock (GameTime_Pause at 0x818FC) and only a
press on the flag-10 panel answers it -- confirm at (120,136) = Smap_SelectTarget, clear at (120,160) =
Smap_ClearSelection -- measured resuming the clock from 357300 to 477300 s. The class 3..7 mission sites the event
engine needs are gated by day counters (nAlienPoolAddend, clamped while < 15 at 0x81B7C), so 122a needs ~15
game-days ~ 220000 pulse steps: minutes with fasttick.
coverage (Part 155): PLAN 122a measured 16.46 game-days in one process (docs/parity/measure_122a_days.sh: 80 blocks of
`fasttick 3000` answered by `tick 1` + `click 120 136`), clock monotone, and the class gate measured live -- classes 0..2
until 15 days, then class 4 at 15.83 days, which is the 0x81B7C clamp under nAlienPoolAddend < 15. Only flag-10 lines and
types 0/1 appeared. Read the flag-23 producers in the IDB: both sit past the arrival gate 0x80368, which needs a Ship with
bIntercepting set (a launched intercept), and SpawnMissionSite's mission-subtype switch (4 alien site/flag 60, 5 intercept/
flag 16, 6 base assault/screen 20) is what the day-gated roll selects. Corrected prose: the flag-10 confirm control's handler
IS Smap_OnPointerUp @ 0x84688 (0x8EC74) -- the port already wires World_SmapPointerUpThunk. Measured negative: a map press at
the recentred (120,160) does not select or launch (status flag 0, list unchanged), so 122a's next step is the card flow whose
ОК is Smap_CancelTarget's Ship_LaunchIntercept at 0x8427c.
coverage (Part 156): audited all 103 test pictures for unfilled rows. Geoscape coverage is pinned in the IDB:
Geoscape_BlitMapView wraps X for every screen id but Y and the second map copy only for id 2 (0x8C3F0 BNE,
0x8C4E4 inside the CMP #2 arm), so for every other id the map is one blit at -(scrollY + nMapH/2 - 160) and the
clamp Smap_ClampScrollY @ 0x837f8 (0x83830/0x8385C) is what keeps the screen covered -- window [-40, 124] on the
shipped 800x400 earth100 with the 84-tall strip at nOriginY 236. Two reference frames (geoscape_ufo_enroute rows
1..41, geoscape_terror_site rows 156..235) came from fixtures writing scrollY by hand outside that window; the
fixtures now call Smap_ClampScrollY and a FixtureViewIsLegal check pins the window (teeth: 2 FAILs without the
call). Four references re-recorded. IDB: 0x187054's comment credited 0x836A0 to the wrong function (it is inside
Smap_HitTestStrategSelect); Smap_ClampScrollY's comment now carries the coverage consequence. Open 156a: the other
15 band pictures, the empty box in econ_buy_craft_queued / econ_hangar_cap_full, ufopaedia_index, the e2e menu bands.

coverage (Part 157): UiPanelMsg_Draw @ 0x3e94c is ported whole -- top rule 0x3E9BC, hovered-row fill 0x3EA44 before
all text, the style pair packed for every row to nLineCount at 0x3EAB8, the per-row rule 0x3EB30, the row's meta pack
0x3EB7C -- with the ARM's LDRB/LDRSB operand split kept apart. 29 references moved and were reviewed one by one
(23 distinct change signatures); every change is a rule blit, the panel's own style pair, or the pair leaking to the
next text drawn anywhere. TestPanelMsgDrawPasses (tests/test_panel.c, 20 checks) is mutation-proved with 13
single-step mutations of src/pufo/ui/pufo_panel.c. The empty box in econ_buy_craft_queued / econ_hangar_cap_full /
transfer_staged / transfer_committed is measured unchanged by this Part and is NOT a panel fill (118 x 17 frame with a
0x00555A interior; no panel of that screen yields (nViewW - 2) x nFont = 118 x 17, and panel E's fill region is
byte-identical to HEAD) -- item 2 of docs/parity/frames-audit.md stays open. New open item 157a: the manuf/research
overlay overlap (econ_manuf_alloc, geoscape_research_allocated), measured as pre-existing -- check whether the overlay
arms blit an opaque pad before their text.

coverage (Part 158): the framed empty box in econ_buy_craft_queued / econ_hangar_cap_full was measured as a stale
surface, not a missing blit -- those two dumps had no frame draw behind them (101 dumps and 611 frame draws counted
with temporary probes in DumpFrame and in World_DrawGeoscapeFrame / World_TactFrame, restored and md5-verified). The
415 px the two pictures lacked against the transfer trio is the picker's confirm caption (packed at 0x98904, drawn as
TEXTS[1267] at 0x98968), which a later press re-blits away through UiControl_HitTest. Both dumps now draw their own
frame; the re-recorded pictures show the buy screen and the shortfall panel, the latter traced band by band to
0x95E18..0x95F80 with the text at 0x789C0..0x78C24 in the 0x789B4 pair. Four further byte-identical dump pairs are
faithful for recorded IDB reasons (0x9877C's erase band, TactHud_DrawModalPanel's mode-1 gate over records 1103/1102,
the research panels' press-only rebuild with the 0x73D64 detail gate, and the base view at 0x8C094).
