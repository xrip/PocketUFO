# New Game startup grants

Date: 2026-10-26

Status: **PROVED for the raw grant table, corrected port table, and the tested
New Game census.** Overall port parity remains incomplete.

## Original raw data and owners

The original `Game_StartNew` is at `0x94554`. Its `Map` literal at `0x94670`
is `0x1139B8`. The object-pointer array starts at `Map+0x108 = 0x113AC0` and
has 149 four-byte pointers, so an object slot `S` is the pointer at
`0x113AC0 + 4*S` (valid slots 0..148). `Map+0x1174` owns BaseRec pointers;
`Map+0x10AC` owns craft pointers. The raw argument setup for the 30 calls,
including MOV/ORR formation and the R1 object-pointer cells, is in the
[read-only ARM audit](../../.workers/luna-normal-ground-loadout/RESULT.md).

The complete original grants are:

| Owner | Object slot and quantity |
|---|---|
| Base 0 stock | 27×1 Avalanche launcher; 52×20 craft-cannon rounds; 53×15 Stingray rockets; 54×10 Avalanche rockets; 112×10 Engineers; 113×10 Scientists |
| Craft 0 cargo | 1×3 Pistol; 4×6 Rifle; 7×2 Auto cannon; 8×2 Heavy cannon; 10×1 Rocket launcher; 14/15/16/17×4 each (Electro-flare, Smoke grenade, Proximity grenade, Grenade); 36×4 Pistol clip; 38×10 Rifle clip; 40/41/42/43/44/45×2 each (Auto/Heavy cannon ammo); 46×2 Small rocket; 47/48×1 each (Big/Incendiary rocket) |
| Craft 1 | 25×1 Craft cannon; 26×1 Stingray launcher |
| Craft 2 | 25×1 Craft cannon; 26×1 Stingray launcher |

`Game_StartNew` passes `R0=BaseRec*`, `R1=ObjectRec*`, and `R2=quantity` to
the base and cargo add routines. Base 0 and craft 0/1/2 come from their live
Map pointer arrays. The full table has 6 base grants, 20 craft-0 grants, and
two grants for each interceptor. Slot 27 is proved by the raw pointer-cell
address `0x113B2C`; its index is `(0x113B2C - 0x113AC0) / 4 = 27`.

The separate `Base_GrantStarterItems` call at `0x95408` adds nothing on this
fresh-New-Game path: the caller passes zero and its raw signed-zero arm returns
before its potential extra grants. The complete raw table and all call sites
are recorded in the [ARM audit](../../.workers/luna-normal-ground-loadout/RESULT.md).

## Port correction and check

The old port indexed unrelated object slots: it used slot 43 for the raw
Avalanche launcher grant and type-0 offsets 67..108 for much of craft-0 cargo.
The independent pre-fix census showed slots 4 (Rifle), 17 (Grenade), and 38
(Rifle clip) at zero, with unrelated slots populated:
[before census](../../.workers/luna-runtime-gate-v2/20261026T162453223Z-0ded4aa6/census-app-output-20261026T162453223Z-0ded4aa6.log).

The localized correction is in `pufo_geoiinit.c`; the independent
`test_startnew` regression checks all six base grants, all twenty craft-0
grants, both fighter cargo columns, the standard slots 4/17/38, and absence of
stray cargo across the object/owner columns. It passes in Release and Debug;
both full CTest runs include it:
[Release](../../.workers/luna-runtime-gate-v2/20261026T183829723Z-2a038cdd/release-all-ctest-20261026T183829723Z-2a038cdd.log),
[Debug](../../.workers/luna-runtime-gate-v2/20261026T184205261Z-f8e58a76/debug-all-ctest-20261026T184205261Z-f8e58a76.log).

The post-fix New Game census reports 20 craft-0 objects with matching
`craftIndexQty` and `shipSlotQty`, including Rifle 4×6, Grenade 17×4, and Rifle
clip 38×10: [census](../../.workers/luna-runtime-gate-v2/20261026T172021552Z-23f14406/census-app-output-20261026T172021552Z-23f14406.log).
The correction changes pointer-array indices only. Shipped `main.dat` values,
prices, capacity rules, and production item labels remain unchanged. The raw
Rifle name has a trailing blank (`Rifle `); the regression checks that archive
string rather than normalizing it.

This proves the tested fresh-start grant path. It does not prove purchase,
delivery, transfer, all New Game branches, or complete data/save parity. The
current full Release and Debug CTests each have one unrelated failing
`test_geoscapeframe`; `test_startnew` itself passes in both.
