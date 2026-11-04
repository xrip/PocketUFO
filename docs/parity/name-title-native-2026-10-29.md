# Final native name/title/load check

Date: 2026-10-29. Result: PASS on the final Release build.
Full evidence, hashes and run paths are in the
[comparison record](name-title-native-2026-10-29.json).

## Last game repair

The title and startup screens wrote the App screen value but left the World's
copy at 22. Original `LoadGame` checks the shared screen value for 17 before
rebuilding the globe UI. A startup load after title/credits therefore loaded
the save but did not restore the playable globe.

`App_HookTitles` and `App_InitStartupUi` now keep both copies in step. The
original stores of 16, 17 and 22 and the load branch are recorded in
[raw IDA evidence](title-load-owner-raw-2026-10-29.json). The redundant thunk
write was removed. The script state observer also now checks the base pointer
before reading it after title mode has freed it.

## Native proof

The same native input driver failed before the repair and passed after it.
It edits the second soldier's name, saves, returns to title, enters and leaves
credits, loads from startup, and saves again. A fresh process loads the saved
slots and confirms all eight names, with only the intended appended glyph.
The passing run is
`.workers/port-goal/native-name-02806edaa7634671a380194f3f746696`.
Its executable matches the final Release hash in the acceptance report.
All native checks ran on a private desktop; the input desktop stayed Default.

## Original function comparisons and IDB correction

The [raw body record](name-title-return-raw-2026-10-29.json) contains all four
bodies (452 bytes), checked against the original PE bytes:

| Original address | Function |
|---|---|
| 0x63138 | Map_BackspaceSelectedName |
| 0x6318C | SoldierScreen_CopyNameToEdit |
| 0x63208 | SoldierScreen_AppendNameGlyph |
| 0x6B46C | OptionsScreen_ReturnToTitle |

The panel constructor and byte-copy instructions prove that `UiPanel.apLines`
holds text buffers. Its IDB type was corrected from `UiControl *[500]` to
`char *[500]`; the member offset, width and whole structure size are unchanged.
The name-copy return type is now `char *`. The IDB was saved and read back.
The mixed root overlay was not retyped.

Both current configurations build cleanly. Release passes 67/67 suites, and
the affected App/startup/input/title suites pass 4/4 in each configuration.
See [final acceptance](../COMPLETION_AUDIT.md) for the full contract result and
the documented platform and evidence limits.
