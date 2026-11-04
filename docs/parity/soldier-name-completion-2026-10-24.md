# Soldier name-entry input parity

> Parent verification (2026-10-24): the integrated tree passes 66/66 CTest suites in both Release and Debug. This supersedes build-status notes below; it does not close the listed untested gameplay paths. Parent integration also connects the handlers and refreshes live panel pointers; the row/keyboard regression passes. See [the completion plan](../PORT_COMPLETION_PLAN.md) for remaining gaps and image/ledger gates.

Date: 2026-10-24

## IDA evidence

Target: `E:\pocketufo\pufobeta.exe.i64`, module `pufobeta.exe`. IDA reported analysis ready. The bounded group contains three independent handlers:

| Original function | ARM address | Size | Checked effect |
| --- | ---: | ---: | --- |
| `Map_BackspaceSelectedName` | `0x63138` | 84 bytes / 19 instructions | Deletes one final name byte when the name is not empty. |
| `SoldierScreen_CopyNameToEdit` | `0x6318c` | 124 bytes / 25 instructions | Clears the name-edit flag, then copies the selected soldier name into the selected roster row, including its NUL byte. |
| `SoldierScreen_AppendNameGlyph` | `0x63208` | 196 bytes / 45 instructions | Converts a tap to one keyboard byte and appends it when the current byte length is below 40 and the text system gives it nonzero width. |

All three calculate the selected soldier pointer from `Map + 0x1198 + 4*nSoldierDetailCursor`; its name begins at `+0xba` (`Soldier.szName`). Backspace calls `strlen`, then stores zero at `name + length - 1`. Copy clears `bNameEditActive` at `0x186b88` before its byte-copy loop. Its destination is `pSolPanelNames->apLines[nSoldierCursor]`, so the detail cursor and roster cursor are distinct inputs.

Append uses signed ARM division: `(y - 80) / 13`, then `(x - 12) / 14`; the stored byte is `column + 16 * (row + 2)`. It writes that low byte to `nNameEditChar` at `0x159de8`, clears the adjacent `unk_159de9` byte, calls `Text_MeasureWidth` on the one-byte string, and calls `strcat` only when width is nonzero. The `CMP R0,#0x28` / `BCS` at `0x6323c` exits before selection-state writes at lengths of 40 or more. Xrefs show the only reference to `0x159de9` is that store at `0x6328c`; `0x159de8` is also read by `SoldierScreen_DrawDetail` for the highlighted keyboard cell.

`Geoscape_OnPointer` supplies the boundaries. At `0x88164` it calls append only while name editing is active and `80 < y < 290`, after setting the popup-visible byte. It assigns the backspace and accept callbacks at `0x88240` and `0x88248`. The event path ignores the return values of all three handlers. Data xrefs also point to callback entries at `0x18a278`, `0x18a280`, and `0x18a288`.

## Port

`pufo_baseui.c` now implements all three handlers with a `SoldierNameEditEnv`. The caller resolves `pSoldier` from `unk_159760` and the roster row separately from `pSolPanelNames->apLines[nSoldierCursor]`, then supplies the live flag and selected-byte storage. Append uses a local two-byte string for `Text_MeasureWidth`; no persistent field is needed for `unk_159de9`, whose only xref is a write. Its width callback is required so tests can supply deterministic results, while the game passes `Text_MeasureWidth`. A bounded scan over the `Soldier` object representation keeps accesses inside the object while retaining the original byte semantics.

The keyboard drawing block was already present and has existing coverage in `TestSoldierDetail`; this change adds the missing input operations. The helpers return `void` because the original event and callback paths discard their results.

## Checks added

`TestSoldierNameEdit` adds ten checks for: deleting one byte; preserving an empty name; closing the editor and copying the full name; mapping the first cell to byte 32; measuring a terminated one-byte string; appending a measured space; mapping row 1/column 2 to byte 50; updating selection without appending a zero-width NUL; accepting a glyph at length 39 and writing its NUL into `bRankType`; and rejecting input at length 40 without changing selected-byte state.

## Known edge and remaining integration

The original `< 40` test accepts input when the name is 39 bytes long. If a glyph has nonzero width, `strcat` places the new byte at `szName[39]` and its NUL at offset `0xe2`, which is the next `Soldier` field, `bRankType`. The port preserves this ARM effect through a `u8 *` view of the complete `Soldier` object, so C does not index beyond the `szName` array. The test asserts the `bRankType` byte becomes zero.

This worker did not wire the helpers into application/world state. Parent-owned wiring must pass the soldier selected by `unk_159760`, the roster row selected by `nSoldierCursor`, state bytes, and `Text_MeasureWidth`. IDB writes, shared builds, and test runs were outside this worker's scope; the note records only read-only IDA evidence and local source/test changes.
