# DatFile body audit: attributes, sharing and owned resources

Status: source repairs reviewed; both full configurations compile. No tests ran.

## Confirmed repairs

The [raw capture](datfile-contract-raw-2026-10-29.json) records original Open,
Close and Exists bodies, with old comments. Exists tests the first find-data
dword with mask1 at0x9F208: FILE_ATTRIBUTE_READONLY, not DIRECTORY0x10. The port
now returns false for read-only matches and true for non-read-only directories.
The prior directory expectation in test_data was wrong and is replaced by
controlled temporary file/directory attributes.

Raw mode1 uses GENERIC_READ/share1/OPEN_EXISTING; mode2 uses
GENERIC_WRITE/share2/CREATE_ALWAYS. The port had used read sharing for writes;
it now passes FILE_SHARE_WRITE for the write-mode opens.

The port's guarded Open could return after acquiring a handle but before setting
nMode. Close and destruction previously skipped that handle when mode was zero.
Shared owned-resource cleanup now closes and nulls the handle and frees/nulls
directory and buffer state. Open's post-acquisition failure exits unwind there
too. Close retains its flush step; destruction does not gain a flush.

## Mode3 and explicit adaptations

Raw mode3 opens GENERIC_WRITE/share2/OPEN_EXISTING at0x9EFB4, then joins the
existing-directory read path. It does not create a fresh zeroed directory.
The port now follows that dispatch instead of overwriting existing metadata.

Microsoft's [Windows CE CreateFile contract](https://learn.microsoft.com/en-us/previous-versions/windows/embedded/aa517318%28v%3Dmsdn.10%29)
distinguishes write-only access, and its
[ReadFile contract](https://learn.microsoft.com/et-ee/previous-versions/windows/embedded/ms891445%28v%3Dmsdn.10%29)
requires read access. Thus this raw mode3 sequence has an access inconsistency.
The original ignores read failures; the existing port guard returns false and
releases resources. No new read-write append capability was invented. No checked
original production caller uses mode3; save/load use modes2/1.

Retained adaptations are explicit: the read-only find handle is released rather
than leaked; selected I/O/conversion/allocation guards return bool errors; local
objects free their own directory. The external ARM allocator's failure contract
is unproven. These are not claims of identical failure-domain behavior.

## What the checks cover

TestDatFileAccessContract checks writable/read-only files and a non-read-only
directory, peer read/write sharing for create/read modes, guarded mode3 failure
with old count/tag/payload retained, and cleanup of a real owned handle in a
constructed pre-mode-publication state. The last case covers Close and dtor;
it is not allocation-failure injection. Inputs are unique owned temporary paths.

The [post-fix review](../../.workers/luna-body-datfile/POST_FIX_REVIEW.md) traces
caller unwind and cleanup. The [baseline audit](../../.workers/luna-body-datfile/RESULT.json)
describes the pre-fix29E81C source; the [body index](body-audit-index-2026-10-29.json)
keeps baseline and current outcomes separate.

Both full builds returned exit0,68 executable outputs and zero diagnostics.
The [manifest](datfile-contract-build-artifacts-2026-10-29.json) records matching
source fingerprints before/after, data hashes and image hashes. Assertions remain
unrun; no game, decoder, debugger, UI or audio device was executed.

Four IDB notes were guarded, read back and saved with DBFL_BAK:
[backup/raw](datfile-contract-raw-2026-10-29.json),
[saved notes](datfile-contract-idb-after-2026-10-29.json).
