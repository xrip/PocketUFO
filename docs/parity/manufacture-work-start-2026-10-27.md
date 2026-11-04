# Manufacture work starts at one after draw

The order UI first records accumulated work0. The original draw/start path
seeds that field to1 when a request has Engineers. This is not an elapsed hour.
The hourly update then adds the assigned Engineer count.

## Raw comparison

| Address | Bytes in memory order | Checked effect |
|---|---|---|
| `0x8BD78` | `98 89 FF EB` | The close dispatch calls the routine at `0x6E3E0`. |
| `0x6E518` | `01 B0 A0 13` | Conditional `MOVNE R11,#1`. |
| `0x6E51C` | `0E B0 84 17` | Conditional store of that1 in the selected work accumulator. |
| `0x70F60` | `00 B0 94 D5` | Load existing work under the hourly branch condition. |
| `0x70F64` | `0B 00 80 D0` | Add the assigned work contribution. |
| `0x70F68` | `00 00 84 D5` | Store the updated work. |

The [raw/source report](../../.workers/luna-manufacture-route-finish/ACCUMULATED-WORK-RESULT.md)
compares the guard, draw caller and hourly path to current C. Parent re-read
the seed/store and hourly instruction bytes before recording this note. IDA
comments and function names were navigation aids, not behavior evidence.
No original behavior deviation or game-code repair was needed.

## Real original-data flow

Research14 completed after360 hours and unlocked item20. The real UI saved one
order with ten existing Engineers. That pending order did not charge money.
For this product the original data requires432 work, so `1 + 43*10 = 431` is
still pending. Across hour44 and status34 acknowledgement, the game produced
one item20, charged31500, and freed all ten Engineers.

[Runtime and cold-load pass](../../.workers/luna-manufacture-route-finish/continuation-20261027T144201Z-sav2-5094e503/RESULT.md):
funds stayed4147000 through hour43 and became4115500 on completion; a fresh
slot3 load retained that amount, stock20 qty1, and ten free Engineers.
The run used captured cc1. Original Research/order checkpoints were preserved;
only the remaining production stages ran. The helper's checksum-literal setup
error was corrected in a private copy, with no game-data change.

This proves the recorded product flow, not every manufacture recipe or final
image. The old checker expectations0 after draw and430 at hour43 were wrong.

