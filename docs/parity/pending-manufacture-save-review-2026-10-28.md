# Pending manufacture across save/load

Status: bounded raw/source review; no new live-owner mismatch confirmed.
No save, test, game or decoder was executed. Original saves remain unavailable.

The [raw capture](pending-manufacture-save-raw-2026-10-28.json) checks product
read/write extents, research header extents, rebuild topic-count setup and legacy
remapping. Work/request/done use separate live arrays; assigned engineers use
the research chunk, not the product chunk.

| State | Current wire extent | World owner |
|---|---|---|
|Accumulated work|6400 bytes|auProductU32|
|Requested units|6400 bytes|anProductReq|
|Completed units|3200 bytes|auProductU16|
|Assigned researchers|0x900 bytes|auResearchCol|
|Assigned engineers|0x900 bytes only|auManufCol prefix|

Raw writes at0x3F580/0x3F59C and reads at0x40710/0x4072C both use0x900-byte
research/engineer headers. The engineer table is larger than that prefix; the
port preserves the original extent rather than inventing a larger save field.
This review does not claim that all high-base assignments persist in the format.

World_StageProductState and World_StageResearchState publish live arrays before
SaveGame. Load publishes product/research state immediately after their readers,
before later migration and derived-state consumers. The UI rebuild used by load
calls Geoscape_InitUI(1): raw0x859E4 sets the topic count to144 before the separate
new-game branch, so a fresh rebuild does not leave migration with a zero count.

Legacy product reads fill shorter prefixes: A/B4608 bytes, C2304 bytes, plus
the1152-byte map at block+16000. World seeds the product buffer from live columns
before reading and joins only the read extents, preserving untouched suffixes.
Migration copies/clears the full A/B/C/engineer arrays, then uses map stride144
and product stride200; raw0x45728/0x4572C advance those exact strides. The migrated
product arrays are joined again before derived flags. Engineers are migrated
through their live array pointer directly.

These are field/owner/order checks, not a full save-format or pending-job runtime
pass. Acceptance still needs a real pending job saved/reloaded through World,
then completed through normal clock dispatch with funds, inputs and output checked.
