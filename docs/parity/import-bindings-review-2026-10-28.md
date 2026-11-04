# Import bindings and return-stub review

Updated: 2026-10-28. Raw binding/return contracts checked; adapter parity pending.
No game, test or debugger executable was launched.

## Import thunk evidence

All85 artifact-class import thunks have the same two raw instruction words:
0xE59FC000, LDR R12 from the literal at PC+0, followed by 0xE59CF000,
LDR PC from [R12]. Each literal contains an actual import-slot address.
All85 slots resolve in the live IDB import metadata: 67 COREDLL, 2 AYGSHELL,
16 fmodce. There are no shape mismatches or unresolved slots in this set.

The [binding ledger](import-bindings-current.tsv) records every original thunk,
instruction words, literal, slot, DLL, import symbol and ordinal, plus source
navigation and adaptation limits. The [raw data](import-bindings-raw-current.json)
preserves the resolved values. Import symbols come from import metadata; source
comments and manually assigned function labels were not used to resolve targets.
This proves the dispatch binding, not the callee's behavior or caller arguments.

## Port adaptation inventory

52 names have exact call-like tokens in current production C outside comments.
Those locations are navigation evidence: a matching token does not prove the
right branch, arguments, widths, ownership or timing.

The33 without direct exact tokens are accounted for as adaptation candidates:

- 16 FMOD imports map to all16 PufoAudioBackend fields and positional WinMM
  table entries. The ledger identifies the field and implementation for each.
  This does not prove all source formats, mixer behavior or timing match FMOD.
- Two AYGSHELL handlers map to deliberate no-op functions in pufo_aygshell.c.
  Their platform scope and game-state dependence still require review.
- Three LocalAlloc/LocalReAlloc/LocalSize thunks have original code callers
  only in the private CRT on-exit registry at0xA1568. Native CRT replaces that
  registry, as described in the earlier CRT review.
- The remaining12 cover allocation/deallocation, strcat, three stdio operations,
  numeric/string conversion, ExtEscape and screen metrics. Their proposed
  replacements and limits are recorded individually.

The three stdio thunks have code callers only in original0x996E4 (transfer
error dump): fclose once, fwrite39 times, fopen once. The port has a real
Transfer_WriteErrorDump using CreateFileA, WriteFile and CloseHandle.
Its byte-format and error behavior are not newly proved by this binding review.

Original mbstowcs callers are registry string access, DatFile open/existence,
and startup. The port uses explicit MultiByteToWideChar(CP_ACP) adapters.
Original strupr callers are chunk begin/find and asset find; the port uses
pufo_strupr for tags. Bounded string helpers, snprintf, code-page choices,
allocation failures and Win64 display geometry remain per-caller contract
checks, rather than blanket equivalence claims.

The two allocation thunks have hundreds of original call sites. Recording
malloc/free as replacements does not establish correct ownership at all sites;
the recently repaired UiControl_FreePads leak is a concrete example.

## Return stubs

Eight entries contain exactly 0xE1A0F00E (MOV PC,LR):
0x163D0,0x22B4C,0x22B50,0x37140,0x61B8C,0x61B90,0x81A28,0x8B948.
They have no body operation beyond returning, and preserve incoming R0.
Any caller that consumes that return still needs the identity/return-use
contract accounted for; they are not eight missing gameplay bodies.

The ninth entry0xA42E0 is different: 0xE8BD9FFF restores R0-R12 and PC from
the stack with writeback. It is not a normal callable empty body. The preceding
instruction at0xA42DC is identical; the following0xA42E4 begins another blitter
with PUSH {R0-R12,LR}. The live IDB reports no xrefs to0xA42E0. This supports
an unused return/continuation interpretation but does not rule out indirect or
unrecovered entry. Its reachability remains unproven, not silently excluded.

## Final disposition of0xA42E0

The [current ARM ABI record](arm-context-restore-2026-10-29.md) replaces the old
empty-stub exclusion. Exact restoration effects are known; native ABI handling
serves the translated code. No independently callable restore routine or
absolute indirect-unreachability claim is made. This is an explicit platform
adaptation, not an unexplained missing game function.

## Historical next work and candidate

The seven CRT bodies/debug stub, 85 imports and nine returns now have scoped
raw statuses in the function ledger. The27 code thunks and six initialization
entries still need caller/argument/lifecycle reconciliation, including the
startup/title repairs already found. Behavior-bearing source functions also
need their scoped evidence carried forward and remaining gaps resolved.

Only evidence files changed in this pass. The current compiled candidate
remains the one in [control-pad ownership](control-pad-ownership-2026-10-28.md).
Runtime acceptance and whole-port parity remain incomplete.
