# Tactical item references and the shared sentinel

The original initializes the shared empty-item sentinel at runtime. Its static
zero data in the IDB is not the value used by inventory serialization.

## Raw comparison

| Address | Bytes in memory order | Checked effect |
|---|---|---|
| `0x3CB2C` | `94 02 9F E5` | Load R0 from a literal containing `0x138EA0`. |
| `0x3CB34` | `7D 1C A0 E3` | `MOV R1,#0x7D00`. |
| `0x3CB38` | `14 10 80 E5` | Store DWORD R1 at `[R0,#0x14]`. |

The [verified initializer/codec report](../../.workers/luna-cold-owners-prepare/addendum/SENTINEL-INIT-VERIFIED.md)
traces the original writer, reader and pool allocator. Current
[Tact_InitMission](../../src/pufo/game/pufo_tactinit.c:786) sets the same
reference value before loading Soldier slots. No production mismatch or repair
was found. Parent re-read the literal and instruction operands, then saved
supporting navigation annotations in the IDB; the annotations are not proof.

| Inventory reference | Meaning |
|---|---|
| `0x7FFF` | Null pointer. |
| `0x7D00` | Shared empty-item sentinel, recreated as that sentinel. |
| `0` | Real item-pool ordinal0; it must not become null. |

The writer forces reserved backpack cells5 and6 to `0x7FFF`, and the reader
restores null there. Only those forced cells may normalize a live null/sentinel
to the same wire-null record. Ordinary null and shared-sentinel slots remain
distinct. A real item in a reserved cell must not be hidden by normalization.

The owner observer must derive ordinary unit/item IDs from actual pool order
and membership. It must not compare transient `nSaveId`/`nRefId` values or
native addresses across processes, and must not classify arbitrary items as
the global sentinel just because a reference field happens to equal32000.
Actual tactical owner-graph roundtrip verification is recorded separately.

