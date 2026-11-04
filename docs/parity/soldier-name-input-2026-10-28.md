# Soldier name input: source connection review

Status: bounded raw/source review; no runtime acceptance. This supersedes the
old bugs.tsv claim that the three edit handlers do not exist in source.

The [raw capture](soldier-name-input-raw-2026-10-28.json) includes Backspace
`0x63138`, accept/copy `0x6318C`, append `0x63208`, screen5 pointer dispatch
around `0x88164`, control binding at `0x88240/0x88248`, and pointer-up handling
at `0x8A000..0x8A034`. This is an on-screen one-byte glyph editor.

The three corresponding functions exist in `pufo_baseui.c`. World callbacks
resolve the displayed Soldier using the detail cursor and call those handlers.
They edit the same Soldier name shown by the detail screen; accept also updates
the roster panel line. The name is read/written as40 bytes by the original save
path. The original length40 edge can write NUL into the following rank byte;
the active port path preserves that edge. No keyboard feature was added.

The separate `pufo_soldiername.c` helper is not used by this World route. Its
safer39-character limit and its tests do not establish behavior of the active
editor. Current source connections establish that the old missing-handler
claim is stale; they do not establish that native clicks visibly edit, backspace,
accept and survive save/reload on the current executable.

Detailed caller, save-width and input findings are in the
[name audit](../../.workers/luna-name-entry/RESULT.md). The separate generated
base-label width repair is documented in [its report](base-name-width-2026-10-28.md).
