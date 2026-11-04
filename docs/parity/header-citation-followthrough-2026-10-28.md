# Header-only citation follow-through

Updated: 2026-10-28. Source bodies/adapters located; raw/full-path parity pending.
No game, test or debugger executable was launched.

## Why this pass was needed

223 non-artifact original functions had address citations only in headers.
A declaration does not prove a body exists or that gameplay reaches it.
The [body map](header-citation-body-map.tsv) now links every one of those223
entries to production C definitions and records source-token references and
any separate production adapter. It supplements the full function ledger.

194 have exact navigation-name definition matches.29 use renamed or shared
helpers, traced from header declarations to definitions. These aliases include
four radio selectors -> Ui_SelectRadio, eight sub-mode variants -> Ui_SetSubMode,
ship/base constructors, side-list operations, line-list accessors, shared item
quantity reads, options-tab helpers, tile lookup and selected-panel scrolling.

Names and header prose locate candidate bodies; they do not establish what
the original does. Alias argument/field differences still require raw checks.
All223 definitions were located after correcting the scan to handle comment,
string and character literals separately. The earlier scan's three Ufopaedia
misses were parser errors, not absent implementations.

## Production references

Fifteen exact-name bodies have no other production C token reference:
twelve convenience save/load slot wrappers (slots1..6), Font_Init_ReturnArg,
StrategList_ctor and DatFile_RegisterAtexit.

The live replacement paths are recorded, rather than treating these names as
proof of a missing connection:

- World Options code installs World_OptionsSaveSlot0..6 and LoadSlot0..6 into
  all seven slot callback entries. Their shared live row helpers call the
  actual World save/load drivers with each selected slot and perform UI state
  changes. The twelve convenience wrappers are not the live dispatch bodies.
- Title/mission font owners call PufoFont_Init directly. This accounts for the
  constructor-return wrapper's initialization effect; complete pre-title
  initialization ordering is still open.
- World_Init calls PtrArray_ctor for its strategList, with explicit World/UI
  teardown replacing the original global strategy-list lifecycle.
- The port's World readers are local DatFile objects with explicit close/dtor
  ownership. Registering those stack objects with process atexit would be wrong.
  Complete failure-path cleanup remains a separate check.

Reference counts include direct-call and function-pointer tokens in C outside
comments/literals. They are not linker reachability or runtime coverage, and
they may include declarations or references from other unreachable bodies.
A zero count is a triage signal, not proof of missing functionality.

## Raw highest-slot comparison

The source table has seven slots0..6, not six. Raw save handler0x6B148:
0x6B154 (0610a0e3) sets R1=6 before SaveGame; it writes list mode4 before
saving, clears the options tab, sets list mode1 and tail-branches to UI close.
The World_OptionsSaveSlot6 binding passes6 and its row helper preserves those
state operations.

Raw load handler0x6B404: clears tab and geoscape screen, sets list mode3 and
the controls-exist byte, calls options teardown at0x6B438, then sets R1=6 at
0x6B43C (0610a0e3) before LoadGame.0x6B448 (ff3010e2) tests the low byte
of the result;0x6B44C/0x6B450 restore list mode1 when zero.
World_OptionsLoadSlot6 passes6, and the shared row helper has the same state
order and low-byte condition. Its return value is not needed by the void UI
callback. This is a scoped raw/source check, not seven successful native I/O
tests or full save-format compatibility proof.

## Disposition

No missing body or new source defect was confirmed among these223 entries.
This does not establish those bodies' correctness or full-game connectedness.
The candidate remains the one in wrapper-lifecycle-2026-10-28.md:
source fingerprint7CDDE20DFBE5670263B443AE14AE1FB06E5C9A1FE7681B63075136B9AE162752.

Continue carrying scoped raw evidence into behavior-function rows and checking
original caller/argument instances. Current candidate gameplay and full suites
remain pending an authorized test host; original-save sample evidence remains
unavailable.
