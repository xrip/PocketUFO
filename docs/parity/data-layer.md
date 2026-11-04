# Parity evidence — data layer

Task: recreate the data layer (main.dat, lang.dat, save files).
Test: `tests/test_data.c` — run from the repo root, reads the shipped archives.

## Files created and the functions they answer to

| Created | Original evidence |
|---|---|
| `dat/pufo_dat.{h,c}` | `DatFile_ctor` @ `0x9eecc`, `dtor` @ `0x9eef4`, `Open` @ `0x9ef38`, `Close` @ `0x9f174`, `Exists` @ `0x9f1bc`, `FlushWrite` @ `0x9f234`, `BeginChunk` @ `0x9f348`, `Write` @ `0x9f4b4`, `FindChunk` @ `0x9f5ac`, `Read` @ `0x9f698`, `ChunkPresent` @ `0x9f7a8` |
| `asset/pufo_asset.{h,c}` | `Assets_LoadStartup` @ `0x382d0`, `GameAssets_Shutdown` @ `0x38e4c`, `AssetReader_Clear` @ `0x9f7d8`, `Find` @ `0x9f7e4`, `Read` @ `0x9f8d0`, `Found` @ `0x9f95c`, `Size` @ `0x9f964`, `ClearGlobal` @ `0x83874` |
| `tests/test_data.c` | new; verifies the above against the real files |

## Format, confirmed byte-for-byte against the shipped archives

```
u32                nEntryCount
DatDirEntry[count] 21 bytes each: char szName[13]; u32 nDataSize; u32 nFileOff;
<chunk payloads>
```

Measured from the real files:

| File | Directory count | First data offset | Check |
|---|---|---|---|
| `main.dat` (10,198,036 B) | 891 | 18,715 | `4 + 891*21 == 18715` — exact |
| `lang.dat` (457,910 B) | 12 | 256 | `4 + 12*21 == 256` — exact |

`main.dat` last entry `TITLE7.DAT` ends at `10196974 + 1062 == 10198036`, the file
size. So the directory stride of 21 is confirmed from both ends of the file.

## Bug found and fixed during this task

`DatDirEntry` is `char[13] + u32 + u32`. A natural C struct is **24 bytes** on
Win64 because the compiler aligns `u32` to 4 bytes after a 13-byte name. The
on-disk stride is **21**. Indexing the directory as an array therefore read
misaligned garbage and segfaulted on the first test run.

Fix: `#pragma pack(push,1)` on the on-disk struct, plus

```c
_Static_assert(sizeof(DatDirEntry) == PUFO_DAT_ENTRY_SIZE, ...);
```

so the packing cannot be silently removed later. The IDA struct is 21 bytes
because the original ARM build packed it the same way.

This is the kind of defect that would have silently corrupted every save file
load rather than failing loudly, so the static assert is the important part.

## Verified behaviours

| Behaviour | Evidence | Test check |
|---|---|---|
| Archive order is lang.dat then main.dat | `Assets_LoadStartup` @ `0x382d0` loops `szAssetPathW[0], [1]` | `TEXTS.DAT` resolves to index 0 |
| Lookup upper-cases the name | `AssetReader_Find` @ `0x9f7e4` calls `strupr` | mixed-case `ObJeCtS.dAt` resolves |
| Archive 0 wins a name collision | `AssetReader_Find` scans 0..1 in order | `TEXTS.DAT` size 330002 (lang copy), not 344202 (main copy) |
| Second archive reachable | same loop | `OBJECTS.DAT` resolves to index 1, size 30992 |
| Object record count = size/0xD0 | `Assets_LoadStartup`: `nObjectCount = size / 0xD0` | 30992/208 == 149 |
| Read refuses to pass the end | `AssetReader_Read`: `nReadPos + cb <= nDataSize` | over-read refused, `nReadPos` unchanged |
| Tag max is 12 chars | `DatFile_BeginChunk`: `len == 0 \|\| len > 12` rejected | 13-char tag rejected |
| BeginChunk writes nDataSize=0, nFileOff=EOF | `DatFile_BeginChunk` @ `0x9f348` | entry 0 offset == 88 == `4 + 21*4` |
| Chunk sizes are per-chunk, not cumulative | `FlushWrite` patches `pDir[nCurEntry-1].nDataSize` | alpha 4, beta 8, gamma 2 all round-trip exactly |

## Write-path detail resolved from disassembly

The IDA pseudocode for `DatFile_FlushWrite` renders the patch address as
`21*nCurEntry - 8 + pDir`, which is ambiguous about which field is written. The
disassembly @ `0x9f234` settles it:

```
LDR  R3, [R4,#8]        ; nCurEntry
MOV  R5, #0x15          ; 21
MUL  R1, R3, R5         ; 21*nCurEntry
SUB  R3, R1, #8         ; -> &pDir[nCurEntry-1].nDataSize
...
MUL  R0, R1, R5
SUB  R1, R0, #4         ; SetFilePointer(21*nCurEntry - 4)
```

So it patches the **previous** entry's `nDataSize`, and offset `0x24` is a real
field (`nChunkFileOff`), not padding as IDA's `gap_34[2]` name suggests. Both are
named properly in `pufo_dat.h`.

The 4-byte `memmove` immediately before the patch only reloads the old value
into a scratch slot that is overwritten on the next instruction. It is vestigial
and is not reproduced.

## Deviations, each with its reason

| Deviation | Reason |
|---|---|
| `DatFile`/`AssetReader` in-memory byte offsets differ from the IDB | pointers and `HANDLE` are 8 bytes on Win64 vs 4 on ILP32. These structs are never written to disk or shared with the original binary, so only named fields matter. The on-disk `DatDirEntry` stride is kept exact and static-asserted |
| `malloc`/`free` instead of `operator new`/`delete` | the original allocation/deallocation entries forward to external COREDLL imports. Their allocation-failure contract is unproven; successful allocations are adapted to caller-owned Win64 storage with explicit failure guards |
| `pufo_strcpy`/`pufo_strncpy`/`pufo_strupr` instead of `strcpy`/`strncpy`/`_strupr` | valid-sized ASCII tag behavior is retained. The port also bounds selected copies; arbitrary overlong strings and locale-sensitive non-ASCII folding are outside this equivalence claim |
| `DatFile_Exists` closes its find handle on the read-only path | raw `0x9F208` tests mask1 (`FILE_ATTRIBUTE_READONLY`), not the directory bit. A read-only match returns0 without `FindClose` in the original; the port preserves that answer and releases the find handle |
| Guarded Open failure and partial-resource cleanup | the port checks selected conversion/open/read/allocation failures. Cleanup releases owned resources even before `nMode` is published; original unchecked failure behavior is not a successful-file contract |
| `MultiByteToWideChar` instead of `mbstowcs` | the port uses explicit CP_ACP conversion. ASCII paths align; exact non-ASCII/locale equivalence remains unproven |
| No original-save interoperability run | the user confirmed no original save sample is available. Port-generated roundtrips do not establish original-save runtime compatibility |

## Checks run

The historical checks below predate the 2026-10-29 body-audit corrections to
attributes, sharing and partial-open cleanup. Changed assertions remain unrun;
their new compile evidence is recorded separately in the completion audit.

| Check | Result |
|---|---|
| `cmake --build build --config Release` | 0 errors, 0 warnings (`/W4 /WX`) |
| `build/tests/Release/test_data.exe` | **PASS, 0 failures** (47 checks) |
| `lang.dat` / `main.dat` untouched | test only reads them; round-trip uses temp files that it removes |

## Residual uncertainty

- `DatFile` in READ mode derives `nCurEntry` from the first empty-named
  directory slot. That matches save files written by `SaveGame` @ `0x44988`
  (which zero-fills the directory and writes entries from index 0), but it is
  untested against a real `.sav` because none exists here. If an original save
  with a directory gap appears, this is the first thing to re-check.
- `DatFile_Open` REWRITE mode (`nMode == 3`) opens with `OPEN_EXISTING` but does
  not reposition to overwrite the old directory. No caller uses mode 3 as far as
  the xrefs show (`SaveGame` uses mode 2, `LoadGame` and `SaveSlot_FormatList`
  use mode 1), so this is carried over as-is without being exercised.
- `TEXTS.DAT`'s leading `u16` is **200** in both archives while the record count
  computed by `Assets_LoadStartup` is `(size-2)/200` (1650 in lang.dat, 1721 in
  main.dat). The 200 is a format marker, not a count. Text record loading is the
  next task's concern; noted here because it is easy to misread.

## The distribution cab's members, and what `0000SKKV.000` really is (Part 108)

`pUFO1.26RUS.cab` holds eight members. Seven are the program the game reads: `00fmodce.001` =
`fmodce.dll`, `000000gx.002` = `gx.dll`, `0000lang.003` = `lang.dat`, `0000main.004` =
`main.dat`, `pufobeta.005` = `pufobeta.exe`, `save_e~1.006` = `save_editor.exe`, plus
`_setup.xml`. The eighth, `0000SKKV.000` (359 bytes), is that cab's own install manifest: it
opens with the CE cab signature `MSCE`, and its strings name the six member files by their
device names (`fmodce.dll`, `gx.dll`, `lang.dat`, `main.dat`, `pufobeta.exe`,
`save_editor.exe`), the install folders `%CE8%\Pocket UFO` and `%CE14%`, and the publisher
block `UFO` / `SMK Software Pocket` / `HPC` / `JUPITER`.

**It is not a save, and no save ships.** The beta imports no cab API (an import-table filter
for `cab` returns nothing at all), and the save path `pufo_save.h` records is
`szAppDir + "sav" + slot + ".sav"` -- a plain `DatFile`. The workspace therefore holds no
original save bytes, and the gate that stands in for one is `test_save.c TestForeignSaveBytes`:
it stages the 359 manifest bytes verbatim as a save file, runs them through the port's
container open, and asserts the count they carry (1162040141) and that no save chunk is
reachable through them.

That path is also where the container's missing validation shows itself. `DatFile_Open` @
`0x9ef38` reads the file's first four bytes as the entry count with the result unchecked
(`0x9EFE8`), sizes the directory with a 32-bit `MUL` at `0x9EFF4` (1162040141 wraps to
`0xAE0C4E51` = 2,928,006,481) and hands it to the throwing `operator new` at `0x9EFF8`. A WinCE
device cannot give that size, so the original's load of such a file ends the process. The port's
multiply widens to 64-bit in the same expression and asks 24,402,842,961 -- measured with
`_msize` in the test, not assumed -- and on a large host it succeeds; the directory scan (the
port's twin of `0x9F030..0x9F058`) then finds the manifest's empty first slot, sets
`nCurEntry = 0`, and no chunk lookup can reach anything. Recorded as a deviation in
`docs/parity/bugs.tsv`.
