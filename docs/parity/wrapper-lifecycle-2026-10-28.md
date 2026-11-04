# Code wrappers and startup lifecycle

Updated: 2026-10-28. Raw wrapper contracts reviewed; complete paths/runtime pending.
No game, test or debugger executable was launched.

## Evidence inventory

The [33-row ledger](code-wrapper-bindings-current.tsv) covers27 code thunks
and six initialization entries. It records numeric branch targets, PC-relative
literal addresses/values and the scoped port adaptation.
[Decoded raw fields](code-wrapper-bindings-raw-current.json) retain instruction
words. The full instruction bytes/disassembly remain in wrapper-raw-current.json.
Source names are navigation aids, not the basis for decoding targets.

Eight CRT/debug entries,85 import bindings,nine return entries and these33
wrappers account for all135 artifact-class claims at an instruction-contract
level. This is not a claim that every adapter or lifecycle path is correct.
The register-restore entry0xA42E0 remains unresolved, and many caller/return-use,
initialization-order, local-reader failure and API contracts remain open.

## Two argument/object bindings checked

- Raw0x18A44 loads R0=0x1139B8, sets R1=1 and branches to0x34FBC.
  The World abort-confirm path calls World_MissionEndCheck(pW,1), which builds
  the current battle-end environment. This checks that wrapper's bound abort
  flag; it does not prove all battle-end branches.
- Raw0x46728 loads the address0x158808, dereferences it, and branches to0x9E5C8.
  The port specializes the blur adapter to g_pTitlesSurface. Titles_ctor installs
  that global from its owned surface; the star-step caller invokes the blur
  adapter and discards its result. Titles teardown clears the global. This
  checks the global-object connection, not every pixel of rendering parity.

The other wrapper rows distinguish process constructors, empty-return callbacks,
item defaults, math initialization, numeric conversions and container/reader
ownership. They do not merely map each wrapper to a callee name.

## Confirmed missing font exit registration

Original startup slot0xA620C points to0x460B4. That entry calls the font
constructor wrapper and branches to registrar0x460D0. Raw0x460D0 loads the
callback pointer0x460DC;0x460D4 branches to atexit0xA1610. Raw0x460DC loads
the font block address0x158268 and branches to the font-free body at0x46144.
The literal/call chain proves the registration and bound object independent
of the assigned IDA labels.

Before this repair the port defined Font_RegisterAtexit and Font_AtexitThunk,
but production never called the registrar. App_QuitDisplay calls World_Free,
Boot_Shutdown and Gx_Close. World_Free does not free the global font;
Boot_Shutdown frees blobs, text tables, boot pads, surface and archives, but
does not free it either. Transition-specific font releases do not substitute
for the missing process registration when quitting from the menu.

App_InitStartupGlobals now calls Font_RegisterAtexit inside its process-once
guard, after the bank registration and registry initialization. The handler
therefore runs before bank cleanup in native reverse registration order,
as in the original font/bank relative ordering. Title rebuilds do not register
it again. PufoFont_Free clears each allocation pointer and resets the counts,
so the source path tolerates an earlier transition-specific release.
Complete startup ordering and runtime exit behavior remain unverified.

## Build/candidate evidence

Both configurations compiled pocketufo,test_appshell,test_geoscapeframe,
test_control,test_uibank,test_baseui,test_geoscape; exit0, zero diagnostics.
Logs: [Debug](../../.workers/luna-mission-init-audit/debug-font-exit-compile.log),
[Release](../../.workers/luna-mission-init-audit/release-font-exit-compile.log).

434-file fingerprint using the existing candidate algorithm:
**7CDDE20DFBE5670263B443AE14AE1FB06E5C9A1FE7681B63075136B9AE162752**.

| Artifact | SHA-256 |
| --- | --- |
| src/pufo/app/pufo_app.c | A47E5B8038B33E6F7B7B3326FFAE4175CA7C58B0EE03C3B645BBC9E022B560CB |
| build/src/pufo/Debug/pocketufo.exe | 9C495D41B49FB11FA56A1AB6053DE7D0C8BC81E3B776AACF18FDC85B2E9FB30F |
| build/src/pufo/Release/pocketufo.exe | 52C6106D468F7BE4F9611FC9F831F968529620C7B8A6725A7D00D03258E12ADD |

This supersedes the control-ownership candidate for runtime verification.
Required host cases: fresh menu exit, repeated title returns then exit,
tactical shutdown then exit, and credits/end-screen transitions then exit.
Check one font registration per process, one final callback, released font
allocations, and no double free after earlier releases. These cases and the
full candidate gameplay matrix have not run. No new runtime test result is claimed.
