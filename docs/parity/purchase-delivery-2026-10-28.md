# Purchase-to-arrival connection review

Status: bounded raw/source review; no confirmed handoff defect. No runtime ran.
Detailed evidence: [worker report](../../.workers/luna-purchase-delivery/RESULT.md)
and [ARM instructions/bytes](../../.workers/luna-purchase-delivery/ARM.txt).

## Connected original and port path

1. Buy controls use the working site at the live site count. Raw `0x761A8`
   writes flag8; `0x761B0..0x761D0` stores the selected BaseRec+4 key at site+1.
2. Row clicks debit funds immediately and stage the order. Soldier row1000
   charges40,000 and increments site+0x202 (`0x75E7C..0x75EF0`). Staff rows112/113
   and ordinary item rows stage quantities in the same working site.
3. Confirm changes that site to flag24 and increments the live count
   (`0x75118..0x75124`). Arrival does not charge funds a second time.
4. The hourly call at `0x5D2FC` reaches `Smap_GrantSiteRecovery`. It decrements
   countdowns and resolves site+1 against live BaseRec+4 values, deriving the
   destination base index (`0x98BF8..0x98C24`). World supplies the live site,
   base, stock, Soldier, craft, next-ID and strategic-list owners.
5. Arrival adds stock (`0x9900C`), creates hired Soldiers (`0x99068..0x99080`),
   or spawns craft products117..121 (`0x9935C..0x993B8`), then marks and compacts
   the delivered site (`0x99484..0x99550`).

No missing callback, stale owner, second debit or wrong destination index was
confirmed in these paths. Capacity treats craft slots117/118 specially; that
does not establish identical capacity rules for every craft product.

## What existing fixtures do not prove

World fixture99.4 checks staff/item orders and delivery stock. The craft fixture
checks craft/home/hangar/strategic state. Both use base0 and direct hourly-hook
calls. Local Soldier-row tests check staging and debit, not a new live Soldier
after delivery. None of these current assertions was run during this audit.

Normal play must leave the base screen after confirm: it closes to screen1,
while the original pulse advances time only on unpaused screen0
(`0x95608..0x9562C`). Acceptance must buy special row1000 for a nonzero base,
return to the globe, advance24 hours through normal time dispatch, then check
the live Soldier roster, base assignment, next ID, funds and order removal.
This remains an open game-flow check, not a claimed failure.

The subsequent [joined source fixture](purchase-clock-fixture-2026-10-28.md)
now covers special row1000 for base1 through World pulse/clock dispatch, with
pending/delivered/nonduplicate assertions on pulses23/24/25. It compiles in both
full configurations but remains unrun. Native timer and game-flow acceptance
are still open; the earlier direct-hourly fixture limits above remain accurate.
