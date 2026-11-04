# Full clean compile checkpoint

Updated: 2026-10-28. **Compile-only; no runtime pass.**

Historical source checkpoint: superseded by the
[staff-row repair/builds](staff-base-row-2026-10-28.md). The identities below
remain evidence for this earlier source only.

Both default builds finished with exit0 and zero compiler diagnostics:

```
cmake --build build --config Debug --clean-first --parallel 2
cmake --build build --config Release --clean-first --parallel 2
```

Each build linked all68 CMake executable targets: the game and67 tests.
Every expected output exists and appears in its clean-build link log.
The default custom targets were inspected before running: they stage runtime
DLLs/licenses; test commands are CTest registrations, not build-time execution.

| Configuration | Linked executables | Diagnostics | Log |
| --- | ---: | ---: | --- |
| Debug | 68 | 0 | [clean build](../../.workers/luna-mission-init-audit/debug-full-clean-compile.log) |
| Release | 68 | 0 | [clean build](../../.workers/luna-mission-init-audit/release-full-clean-compile.log) |

The [artifact manifest](full-clean-build-artifacts-2026-10-28.json) records
all136 executable hashes, runtime DLL copies, game PDBs, log hashes and original
data identities. Counts are build outputs, not passed test suites.

434-file source fingerprint:
**C6F6A0C1AC2B3A1BA9B8788A6A38ADCEBD3FCF61323BC179BBD3D6610882F894**.
This matches the [Back-button repair](startup-list-back-2026-10-28.md) source;
no source changed during the full build. Original main.dat/lang.dat hashes
also match earlier checkpoints.

Clean relinking produced new image identities, which supersede the earlier
affected-target-build hashes for runtime verification:

| Artifact | SHA-256 |
| --- | --- |
| Debug game | 2D5ACCCE25C0657EFD6C6DFFBE220C622820B9CECCC4A5CD9109370472ACD06E |
| Release game | 1E998CBBA4E05C8AE536B01BFDDA327E069F4FD6F0E3097ADB29B179E6A93DB3 |

The current trees now include all rebuilt test executables, including the new
owner/window/phase/audio fixtures. None was run here. No game, debugger, decoder
API or audio device was started. Build success establishes the compile gate only;
it does not establish native launch, playable campaign flows, save compatibility,
or whole-function parity.

Next: execute the fixed gameplay/test matrix on an authorized Windows host,
while continuing the remaining raw/source parity work. The goal stays incomplete.
