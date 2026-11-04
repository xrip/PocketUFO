# Startup slot-list Back button: missing mode clear

Updated: 2026-10-28. Source repaired; affected targets compile. Native flow pending.
No game, test or debugger executable was launched.

## Confirmed missing effect

App_EmptyBtnClick already called EmptyBtn_OnClick with sample and mode-clear
callbacks. The supplied App_HookClearDrawMode body was empty, so the real button
played its sample but omitted the original state write. The header and UI parity
note wrongly described the word as renderer-owned draw mode.

Raw0x1A228:

- 0x1A230 loads sample16 and0x1A234 calls sound.
- 0x1A238 loads its target through the literal at0x1A248, whose value is0x157188.
- 0x1A23C sets R1=0;0x1A240 (001080e5, STR R1,[R0]) clears that32-bit word.
- 0x1A244 returns.

The word is shared by menu list/slot handlers and read by the menu drawing and
input routines. It is the menu list-mode owner, not an isometric drawing
coordinate. [Raw handler/literal](startup-list-back-raw-current.json) is preserved;
the assigned IDA symbol was not used as proof of the store.

## Change and state copies

Renamed the adapter to App_HookClearListMode and restored the zero store across
the App global, World value, and currently staged pointer/input view. These are
existing owners/copies of original0x157188. The App reads its global for drawing
and stages the input copy for dispatch; World frame/save-load paths also own a
copy. Updating all three prevents a stale view from retaining the list mode.

The core helper and header parameter now say ClearListMode. Their signature
types/callback order are unchanged. The existing unit-test helper name was
corrected as well. No new public testing API was introduced.

## Other no-op scan results and limits

The bounded scan covered App, World, AYGSHELL and WinMM adapter files.
- World_NoopClick initializes10 navigation and13 root entries; source assignments
  replace every one of those slots with actual handlers before the tables are used.
  This is not proof that every replacement handler is correct.
- App_HookNoop's original cursor-changed target0x22B50 is a single return.
  No missing state-write body was found there; full caller-return use remains
  outside this scan.
- AYGSHELL's two no-ops and WinMM's window/sample-mode/music-optimization no-ops
  are platform adapters. Their complete caller/output equivalence is not proved
  by a no-op classification and remains in the parity review.

This scan does not exclude conditional or logging-only placeholders elsewhere.

## Verification and required native case

Compiled both configurations, exit0, zero diagnostics:

```
cmake --build build --config Debug --target pocketufo test_control test_mainmenu test_appshell test_geoscapeframe --parallel 2
cmake --build build --config Release --target pocketufo test_control test_mainmenu test_appshell test_geoscapeframe --parallel 2
```

[Debug log](../../.workers/luna-mission-init-audit/debug-menu-back-compile.log),
[Release log](../../.workers/luna-mission-init-audit/release-menu-back-compile.log).

Existing TestEmptyBtnClick checks sample/callback forwarding and ordering with
a mock callback. It would not catch the old App no-op and is not presented as an
App-state regression. It has not been run on this candidate.

On the authorized host: open the startup load list, press the standalone Back
button through native pointer input, require mode0/root-menu display, then reopen
the list and start/load a game. Check an empty list when reachable and a list
containing a port-generated save. Original saves are not needed for this UI case.
Require the current App/World/input modes to agree. No native result is claimed.

## IDB and current candidate

Added a checked store comment at0x1A240; read-back matched and IDA save with
DBFL_BAK returned true. No names/types/code bytes changed. Metadata preimage and
postimage are preserved [before](../../.workers/luna-mission-init-audit/menu-back-idb-comment-before.json)
and [after](../../.workers/luna-mission-init-audit/menu-back-idb-comment-after.json).
The first attempted guard stopped before any write because of Python null
syntax; the successful guarded update used the unchanged preimage.

434-file source/test fingerprint:
**C6F6A0C1AC2B3A1BA9B8788A6A38ADCEBD3FCF61323BC179BBD3D6610882F894**.

| Artifact | SHA-256 |
| --- | --- |
| src/pufo/app/pufo_app.c | 7F5399FEB6117E976A773F93318A339165EE41E1F4B7F3B1428A885101E0FC97 |
| src/pufo/ui/pufo_control.c | 9C27DF60FA1A586163A0C485FE31C10F455D797B8244CFED2F6790224A20CA56 |
| src/pufo/ui/pufo_control.h | A4A78682AB0D543EA4275E7E3FBAB6B2865B55A92A93CD2773D01163869576A3 |
| tests/test_control.c | C0371655D3D321775348E705EB307570AA82E3542C718CC6D2E85A8F5092F9C0 |
| build/tests/Debug/test_control.exe | 8FF4D83D16A80D8955F3E69C53AA0DFFFC8B4079AB960B2BC0FDDD341E4D7F0F |
| build/tests/Release/test_control.exe | 2FECD7D4EDE317866FD163E4E4CF5CF6E80ADC50E80115F1FCC98CAEF6D24961 |
| build/src/pufo/Debug/pocketufo.exe | 1D5199F2DD9174EAF8ECCF4C174D551BF87C52F63A6D5748F4D6AA651740630F |
| build/src/pufo/Debug/pocketufo.pdb | 03C1CA5D584D32BCD75AE18EB13947E15E1164A4AD17834DCE476C81669DAD58 |
| build/src/pufo/Release/pocketufo.exe | F07D0BA2470B5A91FABFC68856423226ADA350365EFCF5080712B1E2B27ABBF6 |

This supersedes the audio-test-inclusive candidate. Source citation/body/import
locations were refreshed. Other full-suite targets still need rebuilding before
the authorized-host acceptance run. The port remains incomplete.
