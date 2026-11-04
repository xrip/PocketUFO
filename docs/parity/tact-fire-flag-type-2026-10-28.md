# TactState+0x0C is an unsigned fire-cancel byte

Checked 2026-10-28. The source and IDB now model this field as u8 bSkipFireShots.
No runtime was executed and no gameplay failure was observed from the old
wider model on the scoped0/1 paths.

## Evidence and correction

The move processor keeps its TactState argument in R4 and reads offset0x0C
with LDRB at0x4FDD0. Nonzero skips a fire command. The fire processor keeps
the same first argument, later reloads it into R7, and stores1 with STRB at
0x51D28. Fire enqueue clears that byte at0x4B0BC before its TU gate.
No word or signed-byte access occurs in these reviewed paths.

The -1 comparison at0x4F8DC instead uses a queue entry's nDelta loaded into
R10 at0x4F428. It is not evidence for a signed state field at0x0C. The port
now uses a byte, direct0/1 writes and a nonzero test; the independent queue
delta is unchanged. The reset test now checks flag1->0 and failure/success
timing, without asserting a fictitious logical upper word. This supersedes
the earlier widened-field interpretation in the frozen EBB59331 snapshot.

## IDB edit audit

- Pass: offset0x0C, width1 and unsigned/nonzero use have raw instruction proof.
- Pass: the member is now unsigned __int8 bSkipFireShots, followed by opaque
  bytes0x0D..0x0F. These bytes are not assigned a meaning.
- Pass: every other existing member offset/size/type is unchanged. Cursor stays
  at0x10 and the existing type extent stays70120 bytes. Preserving that existing
  model size does not prove all other fields or the complete original layout.
- Pass: known owners remain the global TactState pointer at0x157198 and the
  move/fire processor first arguments and constructor type. Readback and
  decompiler application checks show the new member in all three relevant bodies;
  pseudocode is an application check, not the width proof.
- N/A: no exact original/debug-string field identifier was found. The descriptive
  byte-flag name follows the project's naming rule.
- Pass: declaration/member/owner backups, local preview, guarded named edit,
  unchanged-field checks, named readback and database save with backup completed.

The original constructor does not initialize this byte in the reviewed body;
the port already zeroes its whole private state. That existing initialization
difference was not changed. TactState is runtime-only; save layout is unchanged.

[Before declaration and raw reads/writes](tact-fire-flag-type-before-2026-10-28.json),
[local preview](tact-fire-flag-type-preview-2026-10-28.json),
[named readback and application checks](tact-fire-flag-type-after-2026-10-28.json).
[Luna's width/base audit](../../.workers/luna-fire-burst-reset/FIELD_0C.md).

Both full incremental default builds completed with68 executable outputs and
zero compiler diagnostics. [Current source/image identities](byte-native-build-artifacts-2026-10-28.json).
The revised assertions remain unexecuted.
