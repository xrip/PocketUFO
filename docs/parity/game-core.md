# Parity evidence — game core (foundation)

Task: recreate core game: map/world, entities, and game logic.
Tests: `tests/test_mapcell.c`, `tests/test_font.c`, `tests/test_unit.c`,
`tests/test_tactstate.c`, `tests/test_path.c`.

This document covers the foundation layer completed so far. The remaining
game-core work (terrain generation, `Unit_CanStepTo`, combat resolution, LOS) is
listed under "Not yet recreated" with the reason.

## Files created and the functions they answer to

| Created | Original evidence |
|---|---|
| `game/pufo_mapcell.{h,c}` | `MapCell_Init` @ `0x54944`, `Reset` @ `0x54a90`, `TestFlag` @ `0x549c8`, `SetTileSprites` @ `0x548d0`, `HasOccupant` @ `0x5487c`, `SetOccupant` @ `0x548b8`, `SetBFlags57_3` @ `0x548a0`, `SetBFlags29_4` @ `0x548c0`, `ListInsertByPri` @ `0x54b14`, `ListRemove` @ `0x54b58`, `Map_IsValidCell` @ `0x2bc9c`, `Map_IsoOffsetA` @ `0x26f88`, `IsoOffsetB` @ `0x26fa8`, `Map_CenterOffsetX` @ `0x26fcc`, `CenterOffsetY` @ `0x26ffc` |
| `game/pufo_font.{h,c}` | `Font_Init` @ `0x460e8`, `Font_LoadSpr` @ `0x461d8`, `Font_BuildIndex` @ `0x46480`, `Font_GetSprByIndex` @ `0x46600`, `Font_DrawGlyph` @ `0x4661c`, `Font_DrawGlyph_IndexedOffset` @ `0x46630`, `Font_BlitGlyphToSoft` @ `0x9ec48`, `SoftSurf_BlitGlyph` @ `0x9fc00`, `BlitGlyph_Inner` @ `0xa41d4`, `Text_DrawString` @ `0x2d798`, `Text_MeasureWidth` @ `0x2dca8` |
| `game/pufo_unit.{h,c}` | `Soldier_ctor` @ `0x476c8`, `Soldier_Generate` @ `0x49410`, `Unit_ResetBattleState` @ `0x498b0`, `Unit_bIsOperative` @ `0x49eb8`, `Unit_bIsLarge` @ `0x49ecc`, `Unit_PlaceOnMap` @ `0x49a0c`, `Unit_FaceToward` @ `0x49bf0`, `Soldier_RemoveById` @ `0x36b8c`, `Save_WriteSoldiers` @ `0x42350` |
| `game/pufo_tactstate.{h,c}` | `TactState_ctor` @ `0x4f2d4`, `EnqueueTurn` @ `0x49b84`, `EnqueueStep` @ `0x49d5c`, `EnqueueVertStep` @ `0x49ddc`, `EnqueueStandFromKneel` @ `0x49e60`, `EnqueueDeathFlash` @ `0x4a29c`, `EnqueueRecover` @ `0x4a37c`, `EnqueueClearAim` @ `0x4a758`, `EnqueueEndMark` @ `0x4f300`, `KickQueue` @ `0x4f334`, `Unit_EnqueueIfApSpent` @ `0x4a3d4` |
| `game/pufo_path.{h,c}` | `Map_FacingDelta` @ `0x276a0`, `Path_EnqueueCell` @ `0x2b534`, `Path_ExpandCell` @ `0x2b604`, `Map_ComputeReachable` @ `0x2b820` |
| `game/pufo_los.{h,c}` | `Tact_LoadLoftData` @ `0x3bd6c`, `Map_TraceVoxelOccupancy` @ `0x3242c`, `Map_TraceLos` @ `0x30890`, `Tact_LoadPalettes` @ `0x377ec` (palette slots only), `Tact_BuildVisRayTable` @ `0x37ae4` (not ported, see gaps) |
| `game/pufo_boot.{h,c}` | `Assets_LoadStartup` @ `0x382d0`, `GameAssets_Shutdown` @ `0x38e4c`, `Tact_LoadLoftData` @ `0x3bd6c` |
| `game/pufo_template.{h,c}` | `Map_LoadMa1` @ `0x54ba4`, `CDialogTemplate` ctor @ `0x54c3c`, `GetTemplateByte0` @ `0x54c68`, `GetTemplateByte1` @ `0x54c5c`, `GetLevelCount` @ `0x54c74`, `GetCellByte` @ `0x54c80`, `Map_ApplyTemplateToCells` @ `0x54cf4` |
| `game/pufo_rmp.{h,c}` | `Map_LoadRmp` @ `0x55060`, `Map_FreeRmpNodes` @ `0x55018`, `Map_PickRmpSpawnForUnit` @ `0x55418` |
| `game/pufo_tactctl.{h,c}` | `MapCursor_SetFlags` @ `0x26f78`, `MapCursor_IsValid` @ `0x275fc`, `GameSpeed_Set` @ `0x260c0`, `GameTime_Pause` @ `0x5d6ac` |

## Soldier_Generate: a real bug in the original, reproduced on purpose

`Soldier_Generate` @ `0x49410` wraps name generation in a retry loop that is
supposed to redraw when the new name collides with an existing soldier's. The
collision test compiles to:

```
ADD  R1, R1, #0xBA     ; apSoldiers[i] + 0xBA  -> candidate's szName
ADD  LR, R4, #0xBA     ; pSoldier + 0xBA        -> this soldier's szName
CMP  R1, LR / BEQ ...
```

That compares two **addresses inside different objects**, so it can never be equal.
The C source was almost certainly `other->szName == pSoldier->szName` — comparing
array names (pointers) instead of calling `strcmp`. The retry therefore never fires.

The port generates the name **once**, and that is deliberate: a "fixed" retry would
consume extra `Game_RandU16` draws and desynchronise the RNG stream from the
original, which would change every later roster, mission type and stat roll. The
bug is part of the observable behaviour.

Other reproduced oddities in the same function:

- the stat rolls are written in a non-sequential order (`auStat[9]` before `[1]`,
  `[8]` before `[6]`) because that is the draw order;
- `auStatBase[7]` takes `auStat[8]` and `auStatBase[8]` takes `auStat[9]`, i.e. the
  base-stat copy is offset by one at the tail;
- a female soldier whose last-name draw lands below 17 gets a slot from the *male*
  surname range (64..70 only when the draw is >= 17).

Stat bands, asserted in `test_unit`: `[0]` 50..59, `[1]` 40..59, `[2]` 10..59,
`[3]` 30..59, `[4]` 40..59, `[5]` 30..49, `[6]` 10..29, `[7]` 1..100, `[8]` 20..79,
`[9]` 40..69. Gender is roughly one in three, confirmed over 600 samples.

### Text tables extracted

`Soldier_Generate` draws names from tables sliced out of `texts.dat`. The extraction
is now implemented as `TextTables_Extract`, driven by the loop counts and strides in
`Assets_LoadStartup`:

| Buffer | Count | Stride | Bytes |
|---|---|---|---|
| `aszTextB` | 23 | 40 | 920 |
| `aszTextC` (surnames) | 71 | 20 | 1420 |
| `aszTextD` (first names) | 88 | 20 | 1760 |
| `aszTextA` (alien ranks) | 9 | 20 | 180 |
| `aszTextE` (XCom ranks) | 6 | 20 | 120 |
| `aszTextF` | 10 | 30 | 300 |
| `szTextG` / `szTextH` | 1 each | NUL-terminated | <= 30 |

Every source offset into the text blob is an exact multiple of the 205-byte record
stride (221, 198, 318, 230, 192, 181, 1609, 191 records), which independently
confirms the stride. An earlier draft of this note claimed every buffer span closes
exactly on the next global; **that was wrong** and was caught by re-checking: `C`
ends at `0xb1a64` with the next buffer at `0xb1a68`, and `A` ends at `0xb21fc` with
the next at `0xb2200`. Those 4-byte gaps are alignment padding, so the loop count
stays the source of truth. `szTextG`/`szTextH` have no recorded length, so their
capacity is inferred from the following global.

## Formats and structures recovered

### `MapCell` (60 bytes, ILP32)

Field offsets proven from the init/reset masks. `Mc1Rec` is 52 bytes and holds no
pointers, so its size must match on every target — asserted. `MapCell` and
`TactItem` contain pointers, so their ILP32 sizes are asserted only for 32-bit.

| Field | Offset | Proven by |
|---|---|---|
| `pFloorSpr` | 0x00 | `MapCell_SetTileSprites` |
| `pUnit` | 0x30 | `MapCell_HasOccupant` reads `[R0,#0x30]` |
| `pItemList` | 0x34 | `MapCell_ListInsertByPri` |
| `nX`/`nY`/`nZ` | 0x1a/0x1b/0x1c | `MapCell_Init` |
| `nFlags32` | 0x20 | mask `& 0xFFFFFF80` in Init/Reset |
| `nFlags40` | 0x28 | same |
| `bFlags57` | 0x39 | `MapCell_SetBFlags57_3` |

`Mc1Rec` internal offsets, verified against `MapCell_TestFlag`:

| Field | Offset | Note |
|---|---|---|
| `bTransparent` | 0x20 | IDA calls it `gap_22[10]`; compared to **255** |
| `nSlot` / `nGlobalIdx` | 0x26 / 0x28 | set by `Font_LoadSpr` |
| `bBlocksMove` | 0x2a | IDA `gap_2A[0]` |
| `bBlocksSight` | 0x2e | IDA `gap_2A[4]` |

### `.spr` glyph format (confirmed against `lang.dat` `FONT.SPR`)

```
u16 nGlyphCount
u16 anOffset[nGlyphCount]    relative to the member start
<glyph records, RLE>
```

Verified from the shipped file: count **256**, first offset **514 == 2 + 256*2**.

Glyph record, decoded from `SoftSurf_BlitGlyph_Inner` @ `0xa41d4`:

```
u8 nStartY
u8 nStartX
then one byte at a time:
  0xFF  end of glyph
  0xFE  end of row; the NEXT byte is the new nStartX
  else  draw palette index = byte, advance x
```

Independently decoded from `lang.dat` for character `'A'` (byte 65): start (3,3),
10 rows, 68 pixels, first pixel `(3,3,27)`, last `(11,12,27)`. All six assertions
pass against the ported decoder.

Note the `.spr` record shape differs from the `.pad` shape (which uses a u16
width/height header — see `pufo_render.h`). Both exist in the original.

### Pathfinding costs (`Path_ExpandCell` @ `0x2b604`)

```
cost = cur->nUnk20                       (u16)
     + Map_FacingDelta(dir, cur->bUnk24) (turn cost, 0..4)
     + 4
     + (dir < 8 && dir odd) ? 2 : 0      (diagonal)
     + (layer != seedLayer && !flag) ? 4 : 0
```

Neighbour updated only when its cost is 0 or greater than the new value, then
enqueued when `cost < 140`. The `140` cap bounds how far the flood spreads.

Frontier list capacities measured from the IDB: `dword_110AA0` spans 12024 bytes
(3006 u32) and `dword_10DBC0` spans 24024 bytes (6006 u32).

### Voxel space and the LOS trace

Each map cell is **16 x 16 x 12 voxels**. `Map_TraceVoxelOccupancy` converts with
`cellX = x/16`, `cellY = y/16`, `cellZ = z/12` and tests one bit:

```
rowMask = pTable[(y % 16) + 16 * rowIndex]
blocked = (rowMask >> (15 - (x % 16))) & 1
```

Bit 15 is `x = 0`, so the shift counts down. The row index comes from a 12-entry
per-sprite table at record offset `8`, indexed by `z % 12` — i.e. each sprite
carries its own level-to-sprite-index map.

Mask tables, confirmed against the shipped `main.dat`:

| Table | Source | Size | u16 count |
|---|---|---|---|
| `unk_15718C` | `loftemps.dat` | 3584 B | 1792 |
| `unk_1571E8` | `unitloft.dat` | 43008 B | 21504 |

`21504 == 16 rows * 12 levels * 112 sprite slots` — asserted in the test.

Blocking rules, each verified:

| Sprite | Condition |
|---|---|
| Wall A / Wall B | bit **and** (`sprite[0x2a] == 0` **or** `bFlags29` bit 2 clear) |
| Floor | bit only |
| Ceiling | bit only |
| Occupant | bit, via the unit silhouette table; the selected and action-scratch units are skipped |

The extra wall condition is how a wall stops obstructing once the cell's flag bit
is set (opened or destroyed) while still blocking if its block byte is zero.

`word_A9488` @ `0xa9488` decoded to `facing + 8 * kneeling`, i.e. the 16 sprite
rows each unit type occupies. Large units add a `0..3` sub-cell offset derived by
probing the unit's neighbouring cells in the original's order.

### Trace arithmetic (`Map_TraceLos` @ `0x30890`)

```
yaw   = atan2(dy, dx)
bound = sqrt(dz*dz + dist*dist)      where dist = sqrt(dx*dx + dy*dy)
loop:  travel += 1.0                 (one voxel per iteration)
       v = origin + travel * (cos(pitch)*cos(yaw), cos(pitch)*sin(yaw), sin(pitch))
```

A cell change adds the low 3 bits of `nFlags32` to a running cost, **skipping the
values 0 and 4**; the trace stops once the cost exceeds 7. Reaching the bound is
the only path that returns true.

The purely-vertical case (dx == 0 and dy == 0) deliberately randomises yaw from
`Game_RandU16(0x3E) * 0.1` and snaps pitch to ±1.57075. That is the original's
behaviour, not a placeholder, and it is reproduced.

## Bugs found and fixed during this task

Three real defects, each caught by a test or by reading the disassembly instead of
the pseudocode.

1. **`MapCell_HasOccupant` read the wrong byte.** I had assumed `bStatus` was at
   offset 0. The disassembly is explicit:
   ```
   LDR   R0, [R0,#0x30]   ; pUnit
   LDRB  R3, [R0,#0x17]   ; bStatus  <- 0x17
   ```
   Fixed, and the offset is now guarded by a `_Static_assert` against the real
   struct field so it cannot drift. This also surfaced a second-order issue: the
   offset **tracks pointer width** (`bStatus` sits right after `pCell`), so the
   macro is `0x17 + (sizeof(void*) - 4)` rather than a flat `0x17`.

2. **`Path_ExpandCell` had an invented expansion gate.** I had written "expand
   when `nFlags40 != 0`". The disassembly shows the real gate is **`nUnk20 != 0`**
   (cost unvisited) plus a floor-sprite presence test:
   ```
   LDR   R3, [R2,#0x14]  ; nUnk20
   CMP   R3, #0 / BEQ    ; cost 0 -> do not expand
   ```
   Wrong gate meant the flood never propagated and the target was never reached.

3. **Frontier list mapping was inverted.** `Path_EnqueueCell` appends to list B
   when `byte_10DB80` is set, while the expansion arm reads list A in that same
   state — so the flag selects the *enqueue target*, not the list being consumed.
   Rewritten as an explicit two-phase expand-A-then-expand-B loop.

A fourth finding was not a bug: `TactQueueEntry` is 72 bytes on Win64 versus 56 on
ILP32 because it holds pointers. The queue is runtime-only (no `Save_*` function
references `unk_157198`), so byte-exactness buys nothing; the struct documents the
ILP32 offsets and drops the size assert rather than forcing misleading packing.

## Verified behaviours

### Map cell

| Behaviour | Test check |
|---|---|
| `Init` preserves `0x88` of `bFlags29`, clears low 7 of both dword flags | masked values asserted |
| `Reset` preserves a *different* mask set than `Init` | both asserted separately |
| `SetTileSprites` keeps existing sprites when passed NULL | NULL-argument case asserted |
| `TestFlag` selector semantics (`bTransparent == 255`, blocks when byte != 0) | all seven selectors |
| `HasOccupant` ignores `bStatus >= 3` | statuses 0, 2, 3 |
| `ListInsertByPri` is a **two-position splice**, not a sorted insert | head/second-position order asserted |
| `Map_IsValidCell` half-open bounds, layers 0..3 | 9 boundary cases |
| `Map_CellIndex` is `[layer][y][x]` with 10*dimY rows per layer | 4 index checks |

### Font

| Behaviour | Test check |
|---|---|
| `.spr` header parses; first offset `== 2 + 256*2` | assert 514 |
| RLE decode matches the independently decoded glyph | 6 assertions on `'A'` |
| `0xFE` starts a row at a **literal** x, not a delta | blit positions asserted |
| Index 0 is a real pixel, not transparency | asserted |
| Off-surface glyphs are a no-op | asserted |
| Advance table from `fontsize.dat` | `advance[0]==0`, `[space]==3`, `['A']==9` |

### Entities

| Behaviour | Test check |
|---|---|
| Unit type table `byte_A7B80` decoded: types 4-8, 11, 13 are large | all 16 types sampled |
| Out-of-range unit type does not overread | asserted |
| `ResetBattleState` maps `auStat[0]->uTu`, `[1]->uHp`, `[9]->uStunOrArmor` | asserted |
| Type 2 -> 100 armour, type 3 -> 150 | asserted |
| Facing uses **truncated** constants `1.57075` / `0.3926875` | constants pinned |
| All eight compass directions give eight distinct facings; full circle stays 0..7 | sweep of 3600 angles |
| Large unit claims/releases a 2x2 block | all four cells, both directions |
| Edge placement with out-of-range neighbours does not fault | asserted |

### Action queue

| Behaviour | Test check |
|---|---|
| Append order and per-command payloads | 5 helpers |
| Kneeling unit emits a stand entry **first** | 1 vs 2 entries |
| `KickQueue` is a no-op on an empty queue | asserted |
| `DeathFlash` **inserts at cursor+1**, shifting the tail up, when active | order fully asserted |
| `DeathFlash` appends when inactive or the cursor is already last | both paths |
| `uTuSpent >= uHp` completion rule (not a typo) | asserted |
| Flag `0x20` suppresses the flash and the kick | asserted |
| Large unit with HP survives; at 0 HP it flashes | both |

### Pathfinding
| Behaviour | Test check |
|---|---|
| `Map_FacingDelta` wraps the short way; out-of-range returns 0 | 10 cases |
| Straight-line accumulation `seed(1) + 4/step` | 1, 5, 9, 13 |
| Diagonal `+2` surcharge | 5 vs 7 |
| Turning pays the facing delta once, then continues straight | 9, 13 |
| Layer change `+4` | 9 |
| Blank target rejected **before** any expansion | probe-call count 0 |
| Walled-off target unreachable | asserted |
| A wall on one layer is bypassed via another layer (3D flood) | asserted |
| A floorless column on **every** layer blocks the flood | asserted |
| Enqueue limit boundary at 35 orthogonal steps | arithmetic asserted |

### Line of sight

| Behaviour | Test check |
|---|---|
| Bit 15 of a row mask is `x = 0`, bit 0 is `x = 15` | 8 assertions |
| Row index is `y%16 + 16*rowTable[z%12]` | different `y` gives a different row |
| Floor blocks on the bit alone | asserted |
| Wall blocks only when `block byte == 0 || !(bFlags29 & 4)` | all three combinations |
| Occupant blocks via the unit table; dead units do not | statuses 0 and 3 |
| Selected / action-scratch units are excluded | both exclusions |
| Clear ray reaches its target; zero-length ray returns true | asserted |
| A blocking voxel stops the ray | asserted |
| Vertical ray draws yaw in `0..61` | two draws |
| Cost is reported | asserted |
| Real `loftemps.dat` / `unitloft.dat` sizes and row counts | 5 assertions against the shipped archive |

## Deviations, each with its reason

| Deviation | Reason |
|---|---|
| `Unit_RemoveAt` returns the count unchanged for an out-of-range index | the original frees `apSoldiers[count]` (one past the end) and decrements anyway — a heap corruption bug, not a rule |
| `Unit_RemoveAt` compares indices directly | the original's guard `count - 1 > index` is unsigned and underflows when `count == 0` |
| `Path_EnqueueCell` drops the enqueue when the frontier is full | the original has no capacity check and would run past its array |
| `Path_ExpandCell` and `Unit_PlaceOnMap` take lookup/probe callbacks | the originals read `pCtxBase`, `unk_157198` and ~10 globals directly. Injection keeps the algorithms testable without the full game context; the arithmetic is unchanged |
| `TactQueueEntry` offsets differ from ILP32 on Win64 | pointer widening; the queue is never serialised |
| `PufoFont` holds `apSprIndex`/`apMc1Index` as separate arrays | `Font_BuildIndex` builds both from per-slot tables; the flat form is what `Font_GetSprByIndex` and `Font_DrawGlyph` index |
| `Font_DrawGlyph` bounds-checks `nPal > 19` and the index range | the original clamps the palette only (to 19) and indexes `apSprIndex` unchecked |
| `Text_DrawString` does not save/restore the `pCtx+19004` dword | that cell is a text-mode flag the port does not yet model; the visible glyph output is identical |

## Checks run

| Check | Result |
|---|---|
| `cmake --build build --config Release` | 0 errors, 0 warnings (`/W4 /WX`) |
| `test_mapcell` | **PASS** (89 checks) |
| `test_font` | **PASS** (29 checks) |
| `test_unit` | **PASS** (71 checks) |
| `test_tactstate` | **PASS** (59 checks) |
| `test_path` | **PASS** |
| data / render / audio suites | still **PASS** (no regression) |

## Resolved: the `UiPanel` type conflict (was blocking the UI task)

This was found while mapping game-core state. It is now **resolved** and the
conclusion changes how the UI task must proceed.

The two views are real and mutually exclusive:

| Function | Stride | Line data | Count |
|---|---|---|---|
| `UiPanel_InsertLine` @ `0x5a294`, `UiPanel_GetLineColor` @ `0x5a33c` | 16 bytes | colour slots at `+0xC8..+0xD4` | `u8` at `+0xD48` (3400) |
| `UiPanelMsg_Draw` @ `0x3e94c`, `UiPanel_ctor` @ `0x3e730`, `UiScrollList_TrackHold` @ `0x3ed44` | 4 bytes | `apLines[500]` @ `+0xBE4` | `i32 nLineCount` @ `+0xBDC` |

The 7092-byte layout is the real one. Proof from `gUiRootPanel` @ `0x158890`:

```
apLines[105] = 0x158890 + 0xBE4 + 4*105 = 0x159618  ==  nGameTimeSec        @ 0x159618
apLines[106] = 0x158890 + 0xBE4 + 4*106 = 0x15961C  ==  bGeoscapeFlag_15961C @ 0x15961C
```

Both land **exactly** on the globals that `GameTime_Pause` @ `0x5d6ac` and
`SaveSlot_FormatList` @ `0x45904` address as clock fields. Independent
confirmation comes from `UiScrollList_TrackHold` @ `0x3ed44`, whose dword indices
all resolve onto 7092-layout fields: `[754]->nUnk3016`, `[755]->pfnClick`,
`[757]->nUnk3028`, `[759]->nLineCount`, `[760]->nStyle`, `[762..1767]` onto
`nPosY`/`nUnk7052`/`nUnk7056`/`nViewW`/`nViewH`/`nFont`, `[1771]->nUnk7084`.

So `UiPanel` is 7092 bytes and `UiPanel_InsertLine` / `UiPanel_GetLineColor`
operate on a **different, larger structure** that IDA has merged under the same
name. That structure is 3400+ bytes with 16-byte per-line records, and it is the
one the strategy screens actually pass: `Smap_SetWaypoint` @ `0x8549c` calls
`UiPanel_InsertLine` with `gUiRootPanel`'s **address**, i.e. a different object
than the panel the draw path uses.

### What this means for the port

- The `UiPanel` line engine (ctor, free, `Msg_Draw`, `WrapText`, the scroll
  helpers, the line accessors) can be ported against the 7092-byte layout. The
  offsets are proven and `UiScrollList_TrackHold` cross-checks them.
- `UiPanel_InsertLine` and `UiPanel_GetLineColor` must **not** be ported as
  members of that type. They need the second structure recovered first: find its
  allocation size at a call site, then derive its fields from the 16-byte stride
  and the count byte at `+0xD48`.
- Progress on this is recorded in the IDB as comments at `0x5a294` and `0x3e94c`,
  so the next pass starts from the proof rather than from the ambiguity.

## Per-cell light / cover (done)

`Map_RefreshCellMeta` @ `0x2c7f4` is implemented in `game/pufo_mapcell.c`. It is
gated on `gap_86301[1] != 0` (a map without lighting skips the whole pass) and runs
in two phases:

1. set `MapCell.bUnk56` (`+0x38`) to `0xFF` on every cell of every layer;
2. for each source cell, spread an occlusion height over a 23x23 neighbourhood,
   keeping the **minimum** per destination.

### The lightmap format, verified

`Tact_InitMission` @ `0x3c550` reads **0x211 = 529 bytes** of `lightmap.dat` into
`unk_156AB8` @ `0x156ab8`. The shipped file is exactly **529 bytes = 23 x 23**,
confirming the dimensions. The original indexes it as
`unk_156AB8[v19 + 264 + v20]` with `v19 = -253 + 23*k` and `v20 = -11 + c`; that
expression reduces to `23*k + c` and spans exactly `0..528`, which is the whole
table. So:

| Term | Meaning |
|---|---|
| `k` | footprint row `0..22` |
| `c` | footprint column `0..22` |
| index | `23*k + c` |

### Per-source height

```
h = 0
if (pFloorSpr && pFloorSpr[0x1C] != 0) h = pFloorSpr[0x1C]
h = max(h, pWallSprA[0x1C]); max(h, pWallSprB[0x1C]); max(h, pCeilSpr[0x1C])
if ((nFlags40 & 0x78) != 0) h = 1
spread only when h > 0, or an OPERATIVE unit (bStatus == 0) stands there
if the cell has an occupant -> h = 0
```

Sprite byte `0x1C` is `Mc1Rec.bHeight`, newly named here; the value is the maximum
over the four sprites, and the `0x78` test is the same animated-tile field
`GameTick_Step` @ `0x27324` decrements.

### Bug found and fixed

The footprint is **centred** on the source cell: both `dx` and `dy` run `-11..11`.
That is not obvious from the decompilation, which computes the destination as
`base + 4*v18 + 4*v20 + 4*v27` with `v18 = 60*(v26 + v17)`. Reading `v18` as a row
offset gives `y = v26 + v17`, and `v17` runs `-11..11` � signed exactly like the
column offset. My first version used `dy = 0..22`, which shifted the entire footprint
11 rows down. The tests caught it: an expected-`0xFF` cell came back as `10`.

### Verified behaviours

| Behaviour | Test check |
|---|---|
| Disabled pass touches nothing | seeded `bUnk56 = 0x42` survives |
| No casters leaves every cell at `0xFF` | asserted |
| One occluder of height 10 covers its footprint with 10 | centre, `dx = -3`, and layers 1 and 3 |
| Outside the footprint stays `0xFF` | asserted |
| Minimum wins across overlapping sources | taller occluder cannot raise a darker cell |
| The `nFlags40 & 0x78` field forces a contribution of 1 | asserted |
| NULL lookup / lightmap / map refused | three assertions |

## Turn system (done)

`game/pufo_turn.{h,c}` covers `Tact_ProcessActionPhase` @ `0x4d950`,
`Tact_BeginEnemyTurn` @ `0x4ca18`, `Tact_EndEnemyTurn` @ `0x4cb74`,
`Tact_RefreshTeamVisibility` @ `0x4f368` and `TactCtx272_ctor` @ `0x4c9f8`.

`TactCtx272` is 272 bytes and pointer-heavy; the offsets are proven from
`Tact_ProcessActionPhase`, which indexes the opportunity-fire list as
`*(&pCtx->pScanUnit + n)`:

| Offset | Field |
|---|---|
| 0x000 | `pScanUnit` |
| 0x004 | `apOppFire[60]` |
| 0x0F4 | `nOppFireCount` |
| 0x0F8 | `bBusy` |
| 0x0FC | `nPhase` |
| 0x100 / 0x104 / 0x108 / 0x10C | `pSavedFocus` / `pSavedSelUnit` / `pCameraUnit` / `pTrackUnit` |

### Three original behaviours that look like bugs and are not

Each of these was caught by a failing test whose *expectation* turned out to be
wrong, not the code. They are now asserted so a later reader cannot "fix" them.

1. **Phase 2 falls through to the AI tick.** The control flow is
   `if (nPhase == 2) { ...restore...; phase = 3; } else if (nPhase != 3) return;`
   followed by an unconditional `Tact_EnemyAiTick(pCtx);`. There is no `return`
   between them, so a phase-2 call restores the cursor, advances to phase 3, **and
   ticks the AI** in the same invocation.

2. **The opportunity-fire count is decremented twice** on the exhausted path. The
   walk drives it to 0, then the shared tail does
   `v20 = nOppFireCount; nOppFireCount = v20 - 1;`. The count therefore reaches
   **-1**, and because the value tested for the phase transition was 0 rather than
   1, the phase does not advance: the machine stays in phase 1.

3. **The fire coordinates come from `pScanUnit`, not the picked unit.** The unit
   fired with is `*(&pCtx->pScanUnit + nOppFireCount)`, but the coordinates and the
   facing target are read from `pCtx->pScanUnit->pCell` � always index 0. Aiming at
   the picked unit's own cell would fire somewhere the original never fires.

### The opportunity-fire walk

```
while (1) {
    n = nOppFireCount;
    if ((*(&pScanUnit + n)->bFlags & 0x20) == 0) break;   /* eligible */
    n = n - 1; nOppFireCount = n;
    if (n <= 0) goto exhausted;
}
```

It indexes at the **count**, i.e. one past the last valid entry in the usual
count-as-length convention, and walks downward. `test_turn` pins this: with a
count of 3 the first unit examined is `apOppFire[2]`.

### `Tact_RefreshTeamVisibility` @ `0x4f368`

Three raw-offset writes, expressed through the queue struct: the entry at
`nQueueCursor` is marked command 12 (`TACTCMD_END_MARK`, the same code
`TactState_EnqueueEndMark` emits), `nQueueLen` is set to `nQueueCursor + 2`, and
`bFlag1` is raised. Visibility is then refreshed for every XCom unit
(`Unit_UpdateVisibility` @ `0x48580` is injected � 3,728 bytes, its own body of
work).

## Unit_CanStepTo: cell addressing resolved (still not ported)

`Unit_CanStepTo` @ `0x27bb8` is 14,716 bytes with 361 basic blocks and a 10-way
direction dispatch. It is the largest tactical function and decides every legal
move, so it is the gate on real pathfinding.

One thing that made it look intractable is now solved. Several arms index what
looks like a second grid:

```
pMap->gap_TrajRest[14400 * layer + 52284 + 240 * y + 4 * x]
```

That is **not** a second grid. Resolving the absolutes:

| Expression | Absolute | Meaning |
|---|---|---|
| `gap_TrajRest + 52284` | `0x11AF8` (72440) | � |
| `apCells` | `0x15338` (86840) | the real grid |

The difference is `86840 - 72440 = 14400` bytes, which is **exactly one 60x60
layer** (`60 * 60 * 4`). So the "second grid" is `apCells` shifted down one layer,
and those accesses mean `cell(x, y, layer - 1)`. The four offsets used are:

| Offset | Delta | Cell offset |
|---|---|---|
| `+52284` | `+0` | `(dx, dy) = (0, 0)` |
| `+52288` | `+1` | `(1, 0)` |
| `+52524` | `+60` | `(0, 1)` |
| `+52528` | `+61` | `(1, 1)` |

which are precisely the 2x2 large-unit probes in the `case 3` and `case 9` arms.
This also makes the `case 8` / `case 9` vertical-step arms readable: they compare a
cell's value against the cell one layer down, which is the step-down rule.

### Why it is still not ported

The remaining obstacle is not the arithmetic but the coupling: the function reads
and writes ~15 file-scope globals (`byte_10DBB4` direction, `dword_10DBA8` /
`dword_10DBAC` / `dword_113980` the working cell, `dword_113990` / `dword_113994` /
`dword_138E94` the target cell, `g_pCurCell`, `byte_10DB84`, `byte_113984`,
`dword_1139A4`), and it calls `Map_ProbeStepOccupants` @ `0x2778c`, which is itself
1,068 bytes with its own globals. Porting it means porting that whole working-set
contract, which is a task of its own rather than a step in this one.

The port's pathfinding is complete and tested *without* it: the flood fill takes an
injected step probe (`PathStepProbe` in `pufo_path.h`), so the shape of the
predicate is already accommodated. Wiring in the real `Unit_CanStepTo` is a
drop-in once the working-set contract is ported.

## Not yet recreated, with reason

| Function | Size | Why not yet |
|---|---|---|
| `Unit_CanStepTo` @ `0x27bb8` | 14,716 | Cell addressing is now resolved (see above), but it needs a ~15-global working-set contract plus `Map_ProbeStepOccupants`. The path flood injects a probe, so it is a drop-in once that contract is ported |
| `Map_BuildMissionTerrain` @ `0x22e10` | 10,676 | Terrain generation from the `.ma1`/template data. Depends on `Font_LoadTactSprites` and the palette LUTs |
| `Map_QueueDoorAction` @ `0x2bd00`, `Map_ProbeStepOccupants` @ `0x2778c` | 1,448 / 1,068 | Move-resolution helpers; belong with `Unit_CanStepTo` |
| `Combat_ResolveShot` @ `0x2f12c`, `Combat_SplashDamageCells` @ `0x2e7d8`, `Map_TraceLos` @ `0x30890`, `Tact_ProcessFireAction` @ `0x50adc` | 2,828 / 2,388 / 1,948 / 8,192 | Combat, LOS and ballistics. Large and interlinked; the next game-core block |
| `Tact_ProcessMoveAction` @ `0x4f3f0` | 5,868 | The queue *consumer*; the queue itself is done, the driver is not |
| `GameTick_Step` @ `0x27324` | 460 | Animated-cell flag decay + phase counter; small but needs the cell grid wired to the context |
| `Soldier_Generate` @ `0x49410` | 892 | Name generation from `aszTextC`/`aszTextD` and the stat roll. Partially understood; needs the text blobs wired |
| `Unit_FormatDisplayName` @ `0x476d4` | 528 | **ported** — `szDisplayName` is at `Soldier + 0x2bc`, not `+0x2ce`; the two rank tables are runtime-filled `.bss` slots passed in as parameters |
| every `UiPanel_*` line function | — | **Blocked by the struct conflict above.** Do not port until the layout is settled |

## Mission templates (.ma1)

`MapTemplate_Load` @ `0x54ba4` reads `"<base>.ma1"` and stores the header. The
format was confirmed against **all 159 `.ma1` members** of the shipped `main.dat`:
every one satisfies `size == 3 + 4 * dimX * dimY * levels`.

```
u8  nDimX      template width  in map cells
u8  nDimY      template height in map cells
u8  nLevels    number of z levels
u8  aCells[4 * nDimX * nDimY * nLevels]
```

Cell indexing, from `CDialogTemplate_GetCellByte` @ `0x54c80` (the trailing `+3`
skips the header):

```
aCells[4 * (dimX*dimY*z + dimY*y + x) + nSlot + 3]
```

Each cell carries four sprite slots, read by `Map_ApplyTemplateToCells` as floor,
wall A, wall B, ceiling. A slot value of **0 or 1** means "no sprite" (the
original tests `<= 1`); anything above is a real index into the font sprite base.

Samples verified in the test: `AVENGER.MA1` 2403 bytes / header `20 x 10 x 3`,
`CULTA0.MA1` 403 bytes / header `10 x 10 x 1`. The traversal is x inner, then y,
then z, and `test_template` asserts all 600 cells of `AVENGER` are visited with
`SetTileSprites` applied.

## Route / spawn nodes (.rmp)

`Rmp_Load` @ `0x55060` reads `"<base>.rmp"`. Disk records are **24 (0x18) bytes**
and the node count is `size / 0x18`, which **truncates**.

That truncation is not theoretical. Measured across the shipped `main.dat`: **156
of 158** `.rmp` members are exact multiples of 24, but

| Member | Size | `size/24` | Bytes dropped |
|---|---|---|---|
| `UFO_000.RMP` | 140 | 5 | 20 |
| `UFO_010.RMP` | 520 | 21 | 16 |

so the original silently discards their trailing bytes. Reproduced as integer
division; `test_rmp` asserts both counts.

Per-record layout, from the load loop:

| Offset | Meaning |
|---|---|
| 0 | local Y; node Y = `10*tileY + b[0]` |
| 1 | local X; node X = `10*tileX + b[1]` |
| 2 | height; node Z = `level - b[2] - 1` |
| 4, 7, 10, 13, 16 | link target byte (3-byte stride) |
| 5-6, 8-9, ... | link payload (little-endian u16 in the next word) |
| 19 | `bFlag19` |
| 20 | `bFlag20` |
| 21-23 | unused |

So there are **five** links of 3 bytes at offsets 4..18 — not ten. The in-memory
`RmpNode` is 28 bytes with a 10-word `aLink`, so the disk and memory shapes differ:
link `k` occupies `aLink[2k]` (target) and `aLink[2k+1]` (payload).

Link sentinels: `251 -> -5`, `252 -> -4`, `253 -> -3`, `254 -> -2`, `255 -> -1`.

**Confidence note.** The sentinel *values* are certain (each block's `MOV`
immediate is unambiguous). The pairing of *guard condition* to value is
reconstructed: the compiler interleaved those blocks and both the decompiled and
disassembled views disagree on order. Two of the four guards test the same
condition (`nTileY == 0`), which is suspicious but is what both views show. The
mapping is isolated in `Rmp_LinkSentinel` so it is the single place to re-check.

`Rmp_PickSpawnForUnit` @ `0x55418` picks a random start, walks forward with wrap,
and places the unit on the first node that passes three filters: the silhouette
class (`bFlag20`), `bFlag19` against the unit type's size/can-fly flags, and a free
destination cell. `test_rmp` covers every branch of the acceptance predicate.

## Cursor / speed control

`MapCursor_SetFlags` @ `0x26f78` and `MapCursor_IsValid` @ `0x275fc` reach the
cursor through `pMap->pad86309a`, a `char[]` IDA synthesised. Resolving those
offsets against the `Map` type shows the array is pure aliasing:

| Alias | Resolves to | Named field |
|---|---|---|
| `pad86309a[3]` | `0x15125 + 3` | `nCursorX` @ `0x15128` |
| `pad86309a[7]` | `0x15125 + 7` | `nCursorY` @ `0x1512c` |
| `pad86309a[19]` | `0x15125 + 19` | `nLayerScratch` @ `0x15138` |

All three land exactly on named fields, so the port models them by name rather
than through the byte aliases. `MapCursor_IsValid` checks x and y against
`10*dimX` / `10*dimY` but **not** the layer — the cursor carries no layer, so the
asymmetry against `Map_IsValidCell` is deliberate.

`GameSpeed_Set` @ `0x260c0` stores the speed unconditionally and only restarts the
timers for values `<= 4`. A speed above 4 is therefore stored but leaves the timer
set alone; `test_tactctl` asserts both paths.

`GameTime_Pause` @ `0x5d6ac` writes `LOBYTE(gUiRootPanel.apLines[106]) = 1`. Slots
105/106 of the panel's line-pointer array are reused as a packed clock value and a
pause flag rather than as line pointers — `SaveSlot_FormatList` @ `0x45904` reads
slot 105 the same way. Modelling them as named clock fields keeps that aliasing out
of the port.

## Boot / startup load (done)

`Boot_LoadStartup` @ `0x382d0` reproduced in the original's order: open both
archives, then `objects.dat` (count = `size / 0xD0`), `fontsize.dat`,
`texts.dat`, the 240x320 surface, `class.pal` into slot `0x12`, and the three pads
`cs0`/`cs1`/`cs2` at mode 18 with origins zeroed.

Two details that are easy to get wrong, both verified against the shipped files:

| Detail | Value | Why it matters |
|---|---|---|
| texts.dat record count | `(size - 2) / 200`, **not** `size / 200` | The leading `u16` is a format marker. In both archives it happens to be `200`, which makes it look like a record size |
| texts.dat memory stride | `205` bytes (5 bytes of slack per record) | Confirmed from the disassembly @ `0x38718`, where the source advances by `0xCD` = 205 |

Verified against the shipped archives by `test_boot`: 149 objects (`30992/208`),
1650 text records from the `lang.dat` copy (`(330002-2)/200`), a 240x320 16bpp
surface, `class.pal` slot `0x12` entry 0 = `0x0040` and entry 255 = `0xFFFF`, and
`cs0` at 32x32 with palette slot 18.

`Boot_Shutdown` @ `0x38e4c` frees the blobs, releases the pads and the surface, and
closes the archives. One deviation: the original never frees the archive directory
arrays in this function; `Asset_CloseArchives` does it here, which is not
observable to the game.

The path flood, the action queue, the cell layer, the font layer and the entity
predicates are the pieces everything else in `tact` builds on, so they were done
first and are verified against real assets where assets exist.

## Save container and soldier records (done)

Files: `src/pufo/game/pufo_save.{h,c}`. Evidence: `Save_WriteSoldiers` @ 0x42350,
`Save_ReadSoldiers` @ 0x416bc, `SaveGame` @ 0x44988, `Save_WriteStrategVer` @ 0x3f1c8.

A save file IS a `DatFile`. `SaveGame` writes chunks in a fixed order:

    tactver, gamestate, bases, product, research, ships, smap, strateg, time, soldiers
    then, only when bTactFlag != 0: mcnt, tgamedata, objbefore, rmp

Two per-soldier chunks close the roster: `stactflg` (1 byte each) and `1_24scb`
(4 bytes each).

### The `soldiers` record is field-by-field, not a struct image

`Save_WriteSoldiers` writes every field individually, with two different layouts
selected by `bTactFlag`. The non-tactical layout is the one ported, and its field
sizes close to **exactly 740 bytes**:

| region | bytes |
|---|---|
| six flag bits, one byte each | 6 |
| bFacing, uTu, uStunOrArmor, uHp, bMorale | 8 |
| cell nX/nY/nZ, nAnimPose, pad, nDrawOffX, nDrawOffY, bStatus, uTuSpent, nMultiTarget | 12 |
| multi-target block | 120 |
| bUnk148, bUnk149, nLook, bFemale, nUnitType | 11 |
| auStat[10] | 20 |
| nKind, bUnk183, uMissions, szName[40], bRankType | 45 |
| auStatBase[9] | 18 |
| bKneelOrPrimed, bUnk247, bUnk248, uKills, abReady252[5], bUnk961 | 11 |
| auUnk962[25], abUnk1012[25] | 75 |
| inventory block | 150 |
| bHumanoidInv, uInvBurden, bUnk422, nArmorPoints, nClass, nUnk436, nUnk440, bUnk423 | 21 |
| spotted block | 244 |
| **total** | **740** |

Because the record is rebuilt from named fields on every save, the port's Win64
`Soldier` size and padding are irrelevant to the file format � the two are
independent, which is why the Win64 struct may legitimately differ from the
ILP32 original.

### Verified quirks

- **`memset(buf, 0x7FFF, 0x78)` writes `0xFF`, not a `0x7FFF` pattern.**
  `memset` takes an `int` but stores its low byte, so the non-tactical
  multi-target block is 120 bytes of `0xFF`. Reproduced.
- The flags are written as **six separate booleans**, one per bit � three times
  the size of the flag byte � and the read side consumes six bytes too.
- `Save_ReadGameState` @ 0x44958 reads the `gamestate` byte into a *different*
  context cell than `Save_WriteGameState` writes. The port uses one field for both
  and records the discrepancy (documented deviation).
- The `1_24scb` value comes from the linked unit's `nSaveId` at `+956`, with
  `0x7FFF` as the "no link" sentinel.

### Deviations

| # | Deviation | Why |
|---|---|---|
| 1 | `Save_WriteSoldier` requires an already-open chunk; `Save_WriteSoldiers` is the entry point | `DatFile_Write` refuses without an open chunk (verified in `pufo_dat.c`), so the per-soldier writer cannot stand alone |
| 2 | The opaque inventory/multi-target/spotted blocks are written as explicit buffers and skipped on read | The port does not model item save ids yet; the byte stream stays identical |
| 3 | `Save_ReadSoldiers` fills caller-provided storage instead of allocating the roster | Keeps ownership explicit; the original allocates from the count |
| 4 | `gamestate` asymmetry not reproduced | Using two cells would encode a bug with no behavioural payoff |

### Not yet recreated

`LoadGame` @ 0x44bbc � the port implements the
container and the soldier chunks, but the full slot flow also needs everything else in the save subsystem is in place: the container, every chunk writer,
every chunk reader, `SaveSlot_FormatList` @ 0x45904 and `SaveGame` @ 0x44988.

## Unit selection navigation (done)

Files: `src/pufo/game/pufo_roster.{h,c}`. Evidence: `Tact_CycleNextUnit`
@ 0x16684, `Tact_SelectPrevUnit` @ 0x17260, `Tact_SelectNextUnit` @ 0x17304,
`Tact_SelectUnit` @ 0x2c6c4, `MapView_FocusSelected` @ 0x16710.

Roster fields, resolved against the `Map` type: `apXcomUnits` @ 0x15154 (120
bytes = 30 pointers), `nXcomUnitCount` @ 0x1511f (one byte), `pSelUnit` @ 0x15334,
and `gap_2[1]` @ 0x00002.

All three functions share one search loop, verified in the disassembly of
`Tact_CycleNextUnit` at 0x166B4-0x166DC: scan `i = 0..count-1` comparing
`apXcomUnits[i]` against `pSelUnit`. On a hit the index is the found slot; on a
miss it ends at `count` � which the original then uses as a valid index.

The three policies are genuinely different, and the difference is the point:

| function | advance | filter |
|---|---|---|
| `Tact_CycleNextUnit` | `found + 1`, wrap `count -> 0` | **none** |
| `Tact_SelectPrevUnit` | `found - 1`, wrap `0 -> count-1`, do-while | `nUnitType > 3` skipped |
| `Tact_SelectNextUnit` | `++found`, wrap `count -> 0`, do-while | `nUnitType > 3` skipped |

`CycleNext` steps to whatever is next � including a non-human slot � and clears
`gap_2[1]` before selecting. `SelectPrev`/`SelectNext` skip every entry whose
`nUnitType` exceeds 3 (the human soldier kinds) and end by rebuilding the visible
inventory list, which `CycleNext` does not.

### Reproduced quirks

- `CycleNext` computes `found + 1` without checking that the search succeeded, so a
  `pSelUnit` absent from the roster reads `apXcomUnits[count + 1]` � out of bounds.
  The port returns -1 instead of reading past the array (documented deviation).
- The prev/next scan is a **do-while** over `nUnitType`, so a roster with no
  selectable entry never terminates. The port bounds the scan at one full lap and
  returns -1 (documented deviation) � a hang is strictly worse than the original's
  undefined behaviour.
- All three play sample 17 first, before any roster work.

### Deviations

| # | Deviation | Why |
|---|---|---|
| 1 | Missing/invalid selection returns -1 instead of reading out of bounds | The original's read is undefined, not defined |
| 2 | The prev/next scan is bounded at `nCount` steps | The original's do-while can spin forever |
| 3 | `Tact_SelectUnit` and `MapView_FocusSelected` are injected instead of called | Both need the map, status line and cursor; injection keeps the roster testable |

### Not yet recreated

`Tact_SelectUnit` @ 0x2c6c4, `MapView_FocusSelected` @ 0x16710 and
`MapView_FocusOnUnit` @ 0x2c758 � these are the injected seams. `MapView_FocusOnUnit`
additionally needs `MapView_SetCamOffset` @ 0x276f0, `Map_CenterOffsetX` @ 0x26fcc
and `Map_CenterOffsetY` @ 0x26ffc.

## Tactical action modes and UI transitions (done)

Files: `src/pufo/game/pufo_tactui.{h,c}`. Evidence: `Tact_OpenCommandScreen`
@ 0x16ffc, `MapView_LayerUp` @ 0x16530, `MapView_LayerDown` @ 0x164f0,
`Inv_SetFilterAll` @ 0x17410, `Inv_SetFilter0..5` @ 0x17490/0x17610/0x17690/
0x17510/0x17590/0x17710, `Inv_ScrollUp` @ 0x173e4, `Inv_ScrollDown` @ 0x173a8,
`UiBank_SelectRadio0..3` @ 0x16e04/0x16e40/0x16e7c/0x16eb8,
`Tact_SetFireModeAimed/Snap/Auto` @ 0x17790/0x177fc/0x17868,
`Map_PlaySample11_SetMode0B` @ 0x179c0, `Map_UiScreen7_PlaySample0E` @ 0x164b8,
`Map_UiScreen0F_PlaySample0F` @ 0x164d4, `Tact_TryFireMode4/10/5`
@ 0x179e4/0x17a80/0x17b1c, `Ui_FillActionCosts` @ 0x26104.

### Field offsets, resolved against the `Map` type

| field | offset | note |
|---|---|---|
| `nActionMode` | 0x004C | |
| `nUiScreen` | 0x003A | u16 |
| `nUnk40` | 0x0028 | |
| `gap_2[1]` | 0x0002 | the action latch |
| `bUnk1508C` | 0x1508C | inside the `gap15088` region |
| `nLayerScratch` | 0x15138 | the cursor flags |
| `nInvFilterType` | 0x1A22 | u8 |
| `nInvListCount` | 0x4A24 | u16 |
| `nInvListSel` | 0x4A26 | u8 |
| `nTrajFront0` | 0x4A3C | cost/enabled table base |

### The action-cost table is one contract with its readers

`Ui_FillActionCosts` fills, for each mode `m` in 0..11:

    nTrajFront0[4*m + 1040] = cost      (the TU price)
    nTrajFront0[4*m + 1088] = enabled   (whether the mode is offered)
    unk_ACAD0[32*m + 9]     = enabled != 0

The three fire-mode functions read `nTrajFront0[4*m + 1040]` for m = 4, 5 and 10,
resolving to 0x5ABC, 0x5ACC and 0x5B1C, and compare it against the selected unit's
`uTu`. Table and readers are therefore ported together.

Modes 0..2 use the ARM soft-double helpers, and the literal pair
`(1202590843, 1065646817)` is the IEEE754 double `0x3F847AE147AE147B` = **0.01**.
Cost is `auStat[4] * pct * 0.01`, raised by a quarter when `bFlags` bit 0 is set
(kneeling costs more). The other modes are table look-ups transcribed case by case.

### Verified quirks

- **The inventory filter number does NOT match the pad's address order.** Measured
  from the seven `Inv_SetFilter*` functions:

  | filter | pad | pad address |
  |---|---|---|
  | 0 | `pad[0]` | 0xAC9FA |
  | 1 | `pad[3]` | 0xACA5A |
  | 2 | `pad[4]` | 0xACA7A |
  | 3 | `pad[1]` | 0xACA1A |
  | 4 | `pad[2]` | 0xACA3A |
  | 5 | `bFilterPadX` | 0xAC97A |
  | All | `bFilterPadAll` | 0xAC9DA |

  Reproduced exactly. It looks like a transposition but every one of the seven
  functions agrees, so it is original behaviour.
- `Inv_SetFilterAll` writes `nInvListSel = -1` into a **u8**, so the stored value
  is 255, not -1.
- The "All" filter is the only one that does not set `nInvListSel` to its own
  number, and it is also the only one that sets `bFilterPadAll`.
- All seven filter setters play sample 17, zero `nInvFilterType` and rebuild the
  visible list. The two scroll functions do **none** of those things: they only
  move `nInvFilterType`.
- `Inv_ScrollDown`'s guard is `(u32)nInvFilterType + 4 < nInvListCount` � the u8 is
  widened **unsigned** before the add, reproduced as written.
- `MapView_LayerUp` bounds at 4 and `MapView_LayerDown` bounds at **1**, not 0, so
  layer 0 is unreachable through the keyboard path even though it exists.
- `Tact_TryFireMode*` writes `nActionMode` **before** the affordability test, and
  the failure path overwrites it with 12. The latch becomes 4 on success and 0 on
  failure.
- `Tact_SetFireMode*` writes different `nUnk40` values per mode: aimed 1, snap 1,
  auto 3. The out-of-ammo test is `nAmmo == 0 && bAmmoCap != 255`, so an empty
  weapon with capacity 255 (unlimited) is allowed.
- The screen switches play their own samples: `OpenCommandScreen` 16,
  `OpenScreen7` 14, `OpenScreen15` 15, everything else 17.

### Deviations

| # | Deviation | Why |
|---|---|---|
| 1 | `Ui_SetFireMode` refuses a NULL weapon instead of dereferencing it | A crash is not behaviour worth reproducing |
| 2 | The ammo scalars are passed as a small struct rather than through `TactItem` | `pufo_mapcell.h` models a minimal `ObjectRec` without `bAmmoCap`; the full record is `ObjectRecFull` in `pufo_boot.h` |
| 3 | Status messages pass a text-record index instead of a string | The messages live in runtime text records the port has not wired up; `byte_B3D9E` and `aTextRecs+205` both read as 0xFF placeholder data in the IDB |
| 4 | Out-of-range filter/radio/mode values return false without touching state | The original indexes unconditionally |

### Not yet recreated

`Tact_OpenLeftHandMenu` @ 0x17030 and `Tact_OpenRightHandMenu` @ 0x17148 � both
need `Ui_FillActionCosts`' caller context plus the `aFacTypeTable`/`byte_AA968`
item-category tables at 0x15EF08 and 0xAA968, which belong to the inventory
subsystem. `Tact_TryFireMode10` @ 0x17a80's `nTrajFront0[1080]` reader is covered
by the ported cost table.

### Weapon sub-modes and option toggles (done, same module)

`Map_SetSubMode0..8_Play11` @ 0x18418/0x18478/0x184d4/0x18534/0x18594/0x185f4/
0x18654/0x186b4/0x18714 are nine copies of one body with a single differing
constant:

    Sound_Play(17)
    pSelWeapon->bUnk8 = 1
    pSelWeapon->bUnk7 = nSubMode            (0..8)
    pSelUnit->uTu -= anActionCost[nActionMode]

The TU subtraction reads the **current action mode's** cost, so it depends on
`Ui_FillActionCosts` having run first. The subtract is a plain u16 wrap-down; the
original does not clamp at zero.

The five `Map_ToggleFlag128A8D..91_Play11` functions @ 0x19250/0x192d4/0x19348/
0x193a0/0x1941c are **not** five copies of one toggle. Their option bytes are
`anActionPhaseSave0[93..97]`, which resolve to 0x150D5..0x150D9 � exactly the
option bytes the `pufo_options.h` header documents (`scrollbypen`, `showborder`,
`runbydblclick`, `turnbypen`, `menubypen`). The same bytes are reached here through
a raw index rather than by name. Each has its own coupling rule:

| option | rule |
|---|---|
| SCROLL | plain toggle, but turning it **off** also clears SHOW and SHOW's pad |
| SHOW | does **not** flip: it is recomputed, and can only be on while SCROLL is on |
| RUN | plain toggle |
| TURN | turning it **on** clears MENU |
| MENU | turning it **on** clears TURN |

Each pad indicator at 0xACF9A/0xACFBA/0xACFDA/0xACFFA/0xAD01A mirrors its byte.

Screen helpers, all verified: `Map_ClearUiScreen` @ 0x18a54 (no sound),
`Map_PlaySamples10_0E_ClearUiScreen` @ 0x18e9c (16 then 14), `Map_UiScreen7_
PlaySamples10_0E` @ 0x194f8 (screen 7 first, then 16 and 14),
`Map_PlaySamples10_7_Set/ ClearFlag_ClearScreen` @ 0x19528/0x19568 (16, 7, then
`bDeployPanelOpen` at `gap_86272[16]` = 0x15110), `Tact_OpenCommandScreenFlagged`
@ 0x198c4 (`LOBYTE(apUiSlotsA[8])` = 0x80 set, and it does **not** clear 0x1508C �
unlike the un-flagged variant), and `Map_Inc/DecByte128AF0_Play11` @ 0x19670/
0x196ac (same 1..4 layer bounds as LayerUp/LayerDown but **without** calling
`MapCursor_SetFlags`).

## Win32 timer set (done)

Files: `src/pufo/game/pufo_timer.{h,c}`. Evidence: `KillTimers_A` @ 0x9e6a4,
`StartTimers_A` @ 0x9e6d4, `StartTimers_B` @ 0x9e714, `KillTimers_B` @ 0x9e754,
`StartTimers_C` @ 0x9e784, `KillTimers_C` @ 0x9e7c4, `StartTimers_D` @ 0x9e7f4,
`KillTimers_D` @ 0x9e864, `KillTimers_BySpeed` @ 0x9e8b4, `StartTimers_BySpeed`
@ 0x9e904.

Every call uses the global `hWndMain` @ 0x188d54 and one of five ids:

| id | role | note |
|---|---|---|
| 0xC350 | GameTick | interval driven by `nGameSpeed` |
| 0xC351 | Render_Frame | same interval as GameTick |
| 0xC352 | animation | 25 ms |
| 0xC353 | UI pulse | 50 ms |
| 0xC354 | extra | only `StartTimers_D` starts it, at 10 ms |

### A, B and C are three identical pairs

`KillTimers_A/B/C` and `StartTimers_A/B/C` are **byte-identical**:

    Kill  A/B/C : kill 0xC351, 0xC352
    Start A/B/C : 0xC351 = 40 ms, 0xC352 = 25 ms

That redundancy is in the original. The port keeps all three names so call sites
stay recognisable, and the test asserts all three produce the same requests.

`StartTimers_D` is the outlier: four ids, `0xC351 = 0, 0xC352 = 50, 0xC353 = 20,
0xC354 = 10`. The **zero interval is reproduced verbatim** � Win32 treats a zero
interval as "fire as often as possible", not as "disabled", so correcting it would
change behaviour.

### The speed-driven set

`StartTimers_BySpeed`, which `GameSpeed_Set` @ 0x260c0 calls, sets `0xC350` and a
matching `0xC351`:

| speed | interval |
|---|---|
| 0 | 100 ms |
| 1 | 50 ms |
| 2, 3, 4 | 10 ms |
| 5+ | **neither timer is set** |

Then, for **every** speed, it sets `0xC352 = 25 ms` and `0xC353 = 50 ms`. So an
out-of-range speed leaves the two game-rate timers alone while still restarting
the animation and UI timers � the switch simply has no default arm. Reproduced
exactly, and the test asserts the asymmetry.

`KillTimers_BySpeed` unconditionally kills `0xC350..0xC353`, including `0xC350`
which a speed of 5 or more may never have set.

### Verified quirks

- `StartTimers_D`'s `0xC351 = 0` is a live "as fast as possible" timer, not a
  disabled one.
- Speeds above 4 set no game-rate timer at all, while still setting two others.
- `KillTimers_BySpeed` kills four ids unconditionally, so it can kill an id that
  was never started.

### Deviations

| # | Deviation | Why |
|---|---|---|
| 1 | The window handle is passed in via `Timer_SetWindow` instead of read from a global | The port keeps `g_hWndMain` in the app shell; the original reads `hWndMain` @ 0x188d54 directly |
| 2 | `SetTimer`/`KillTimer` are injected through `Timer_SetBackend` | Win32 timers are the only untestable part of this layer; the id/interval table is the behaviour that matters |
| 3 | `Timer_InstallAsGameTimers` takes the `GameClock*` | `GameTimers`' callbacks carry no speed argument, and `GameSpeed_Set` writes the clock immediately before calling `pfnStartAll`, so the bridge reads it back from there rather than keeping a second copy |

This closes the loop on `GameSpeed_Set`'s previously no-op timer backend: the real
timer set now exists and can be installed with one call.

### Not yet recreated

`GameTick_OnTimer` @ 0x274f0 (268 bytes) and `UiPulse_OnTimer` @ 0x36630 (488
bytes) � the two `WM_TIMER` handlers these timers post to. Both are dispatch
functions whose bodies call into subsystems (render frame, UI pulse) that are only
partly ported.

## Facing helpers (done)

`Map_FacingDelta` @ 0x276a0 and `Map_bFacingTurnPositive` @ 0x27668 both live in
`src/pufo/game/pufo_path.{h,c}`. They are the two ways the original asks about a
turn, and they agree except at exactly opposite facings:

    Map_FacingDelta(from, to)         -> circular distance, 0..4
    Map_bFacingTurnPositive(from, to) -> is this a positive (clockwise) rotation

`Map_bFacingTurnPositive` is transcribed as written:

    v3 = to - from;
    v4 = 1; if (v3 < 0) v4 = -1;
    if (abs32(v3) > 4) v4 = -v4;
    return v4 > 0;

The threshold is **`> 4`, not `>= 4`**, so a diff of exactly 4 (directly opposite)
is NOT flipped and the raw sign wins. `Map_FacingDelta` substitutes at the same
point (`nDiff > 4`), so the two agree about which way round is shorter everywhere
except the exactly-opposite case, where the sign test simply keeps the raw
direction. The test pins that boundary explicitly.

## Options-menu buttons (done)

Files: `src/pufo/game/pufo_menu.{h,c}`. Evidence: `MenuBtn_SetSpeed0..4`
@ 0x18ecc/0x18f20/0x18f74/0x18fc8/0x1901c, `MenuBtn_SetGamma0..4`
@ 0x19070/0x190d0/0x19130/0x19190/0x191f0, `Options_SetMusicVol1..5`
@ 0x19a10/0x19a64/0x19ab8/0x19b0c/0x19b60, `MenuBtn_SetSfxVol1..5`
@ 0x19bb4/0x19c14/0x19c74/0x19cd4/0x19d34, `MenuBtn_EndTurn` @ 0x16494,
`MenuBtn_ResizeWindow` @ 0x19930.

### These write the same option bytes `pufo_options` owns

Twenty buttons, and every one of them writes into a context field the options
module already documents by name:

| button group | field | offset | reached in the original as |
|---|---|---|---|
| speed | `nGameSpeed` | 0x150FC | direct |
| gamma | `nGamma` | 0x15100 | `*(_DWORD *)gap_86272` |
| music | `nMusicVolume` | 0x150E0 | `anActionPhaseSave0[104]` |
| sfx | `nSoundVolume` | 0x150DC | `anActionPhaseSave0[100]` |

`0x15078 + 104 = 0x150E0` and `0x15078 + 100 = 0x150DC`, which is exactly how the
original reaches the two volume fields through a raw index. So the menu layer
drives the same option block the options screen persists; the port passes that
struct in rather than duplicating fields.

### One-hot pad arrays

Each button also lights one of five pad bytes. The four groups sit at a 0x20
stride and are identical in shape:

    0xACE5A speed   0xACEFA gamma   0xAD09A music   0xAD13A sfx

Every handler clears all five pads of its group and sets one, so the arrays are
one-hot at all times. The five speed pads also encode the position, so the lit
index IS the selected value for every group.

### Per-group differences that are real

| group | order of effects |
|---|---|
| speed | sample 17, pads, `GameSpeed_Set` |
| gamma | sample 17, pads, write gamma, `Tact_LoadPalettes` |
| music | **no sample at all**, write volume, `Sound_ApplyMusicVolume` |
| sfx | write volume, `Sound_ApplySfxVolume`, **then** sample 16 |

- The **music group is the only group that plays no sample**: `Options_SetMusicVol1..5`
  contain no `Sound_Play` call whatsoever.
- The **sfx group plays its sample last**, after applying the volume, while every
  other group plays first. Verified from `MenuBtn_SetSfxVol1`/`5`, whose trailing
  `return Sound_Play(16)` follows the volume apply.
- The volume functions are **named 1..5 but index their pads 0..4**; the naming is
  the original's and the pad index is 0-based in all four groups.

Because the speed group calls `GameSpeed_Set`, it inherits that function's
documented behaviour: only speeds 0..4 restart the timers. Only 0..4 have buttons,
so the out-of-range branch is unreachable from the UI.

### Deviations

| # | Deviation | Why |
|---|---|---|
| 1 | The handlers take `PufoOptions*` and `GameClock*` instead of the monolithic context | The fields are the same bytes; passing them by name is what the options module already does |
| 2 | `Sound_ApplyMusicVolume`/`Sound_ApplySfxVolume`/`Tact_LoadPalettes` are injected | They read the option block and the palette store themselves in the original; injection keeps the buttons testable |
| 3 | `Tact_BeginEnemyTurn` and `App_ResizeMainWindow` are passed as callbacks to `Menu_EndTurn`/`Menu_ResizeWindow` | Both need the whole world; `Tact_BeginEnemyTurn` is already ported and can be passed in directly |

### Not yet recreated

The remaining `strat-ui` menu buttons � `Menu_OpenLoadSlots` @ 0x18864,
`Menu_OpenSaveSlots` @ 0x188a4, `Menu_CancelQuitDialog` @ 0x18e04,
`Menu_ConfirmQuitDialog` @ 0x18e2c, the `SaveLoad_Slot*` family, and the
`UiModal_Begin` dialogue machinery @ 0x26430 they depend on.

## Status message stack (done)

Files: `src/pufo/game/pufo_statusmsg.{h,c}`. Evidence: `StatusMsg_Push` @ 0x2dda4,
`StatusMsg_Shift` @ 0x2dd20, `StatusMsg_DrawStack` @ 0x1afc4.

### Layout, resolved against the `Map` type

| field | offset | size |
|---|---|---|
| `nStatusMsgCount` | 0x0054 | i32 |
| `anStatusMsgTTL` | 0x1408C | i32[10] declared |
| `aszStatusMsg` | 0x140B4 | 1500 = **15 records of 100** |
| `nStatusDrawY` | 0x002C | i32 |
| `pHudPad` | 0x00FC | |

The 1500-byte array size is where the `>= 15` guard comes from.

### The push policy is not a simple stack

    if (!bForce && busy) return;                  // dropped
    if (count < 3)              { append; ttl = 90; }
    else if (!busy || count >= 15) { Shift(); append; ttl = 90; }
    else                        { return; }        // dropped
    if (renderFlag) Render_Frame();

Below three messages a push **always** appends. At three or more it drops the
oldest and appends only when the game is **not busy** or the buffer is completely
full. A busy game with 3..14 showing discards the new message entirely, even a
forced one. Every stored message gets TTL **90**, regardless of length.

### Finding: the 15-record capacity is unreachable

This fell out of writing the test. With `count == 3`:

- **not busy** -> the middle branch shifts the oldest out and appends, so the count
  returns to exactly **3**. It can never grow.
- **busy** -> the message is dropped, so the count stays at 3.

Either way the count saturates at 3, so `count >= 15` can never be true and the
capacity arm is **dead code**. The 1500-byte allocation (15 records) is therefore
four times larger than any reachable state needs. The port reproduces the
behaviour and keeps the 15 constant, because that is what the original declares �
but it is a real inefficiency in the original, noted here rather than "fixed".

The interesting consequence is that the busy flag decides whether a full stack
**slides** (not busy: newest replaces oldest) or **drops** (busy: the window is
frozen). The test asserts both directions explicitly.

### Shift

Copies records `i-1 <- i` for `i = 1..count-1` (both text and TTL) and decrements
the count. Index 0 is the **oldest**, so the shift drops it. The decompilation's
`p_nMultiTargetCount[20514] = p_nMultiTargetCount[20515]` resolves to that TTL
copy starting at i = 1.

### Draw

`StatusMsg_DrawStack` walks newest-first. The original's source expression
`&nFireArg6 + 100 * (count - i)` resolves through `0x14050 + 100*k`, and
`aszStatusMsg[0]` is at `0x140B4 = 0x14050 + 100`, so the first record drawn is
`aszStatusMsg[count-1]`. The original reuses `pInvPad` as the loop counter, which
is why a `PadImage*` field appears in a draw function; the port uses a local.

The y arithmetic is reproduced exactly: base 320, or `pHudPad->nOriginY` only when
player input is enabled **and** a selected unit exists **and** it is operative
**and** the game is not busy; then `-14` once, then `-12` per already-drawn record.
The colour pair `(255,255,55)/(90,90,10)` is packed unconditionally, even for an
empty stack.

### Deviations

| # | Deviation | Why |
|---|---|---|
| 1 | `Status_Store` truncates at 99 bytes | The original's strcpy loop overruns the 100-byte slot; a buffer overrun is not behaviour worth reproducing |
| 2 | Busy/render/layout effects are injected through `StatusHooks` and `StatusDrawHooks` | The busy flag lives in `TactCtx272`, the render call needs the whole world, and drawing needs the glyph layer |
| 3 | The unreachable 15-record arm is kept rather than removed | It documents the original's declared capacity; deleting it would hide the finding |

### Filtered unit selection (done, same module)

`Tact_SelectPrevUnitFiltered` @ 0x19708 and `Tact_SelectNextUnitFiltered` @ 0x197b8
are in `src/pufo/game/pufo_roster.{h,c}`. They look like the unfiltered pair but
differ in two ways that matter:

1. **A second filter term.** A non-human (`nUnitType > 3`) is skipped only while
   `LOBYTE(apUiSlotsA[8]) != 0`. When that byte is clear the first non-human
   encountered is **accepted** rather than skipped. So the filter flag turns "skip
   every alien" into "stop at the first alien".
2. **The next variant acts per candidate.** It calls `Tact_SelectUnit` and
   `MapView_FocusSelected` *inside* the loop, once for every candidate it examines,
   and re-reads `nXcomUnitCount` from the context each pass. A next-selection that
   walks past two aliens therefore selects and repaints three times before settling.
   The prev variant tests both terms in one while guard and acts once.

The per-candidate selection is a visible behaviour (three status messages and three
camera moves), not an implementation detail, so the port reproduces it and the test
asserts the call counts: **3 selects / 3 focuses / 1 list rebuild** for the next
variant, versus **1 / 1 / 1** for prev.

Deviations: both walks reuse the same bounded-step protection as the unfiltered
pair, so a roster where nothing is acceptable returns -1 instead of spinning.

## Fire burst queue builder (done)

`TactState_EnqueueFireBurst` @ 0x4b080 is implemented in
`src/pufo/game/pufo_tactstate.{h,c}`. It is the real body behind the fire request
that `Tact_ProcessActionPhase` injects, so the turn system now has a concrete
implementation to install instead of a placeholder.

The queue entry gained three fields that only this function writes (`nArgDword`
0x20, `nTuCost` 0x24, `nMode` 0x28 in the ILP32 56-byte entry), verified from the
fires-burst disassembly: the shot entry writes `STRB #0xE` at entry+0x18 (the
command byte via the 0x38 stride) and the coordinate bytes at 0x21/0x22/0x30
relative to `TactState`, which are entry+0x09/0x0A/0x18.

### Verified behaviour

1. **The TU check comes first and is strict.** `uTu < nTuCost` returns immediately
   with no queue writes and no facing, so `uTu == nTuCost` is accepted.
2. `Unit_FaceToward` is then called (injected, because it needs the unit's cell).
3. The shot count:
   - mode 7 -> `nMultiTargetCount`
   - mode 0 -> **3**
   - everything else -> 1
   Outside mode 7 only, the count is capped by the weapon's loaded ammo **unless**
   the capacity byte is 255 (unlimited).
4. Each shot appends **two** entries: command 14 (shot, with all parameters) then
   command 15 (mark, with just the unit). So `nQueueLen` grows by two per shot.
5. `TactState_EnqueueClearAim` (command 10) is appended last.

### Quirks worth pinning

- **Only the first shot carries the TU cost**; every later shot and every mark entry
  gets 0. A three-shot auto burst therefore charges one cost for three shots.
- **The ammo cap does not apply in mode 7**, so a multi-target burst fires one shot
  per target even with an empty magazine.
- **Zero loaded ammo with a real capacity queues only the clear-aim entry** � the
  shot count legitimately drops to 0. Easy to mis-assume a minimum of one.
- Coordinates come from `apMultiTargets[i]`'s cell in mode 7 and from the single
  `(nX, nY, nZ)` argument otherwise.

### Deviations

| # | Deviation | Why |
|---|---|---|
| 1 | The weapon's ammo scalars are passed in a `FireBurstSpec` rather than read through `TactItem.pObj` | `pufo_mapcell.h` models a minimal `ObjectRec` without `bAmmoCap`; the full record is `ObjectRecFull` in `pufo_boot.h` |
| 2 | The capacity is guarded before the write loop | The original has no guard; its queue is sized far above any reachable burst. A silent overrun is not behaviour worth reproducing |
| 3 | The three context scratch fields the original clears are left to the caller | They belong to the renderer and the shot animator, not to the queue |

### Action confirmations (done, same module)

`TactAction_ConfirmUseItem` @ 0x178d4 and `TactAction_ConfirmFireBurst` @ 0x18774
are in `src/pufo/game/pufo_tactui.{h,c}`.

**ConfirmUseItem** (action mode 3): sample 17, mode 3, then the mode-3 cost is
compared against `uTu` (equality allowed). Enough TU branches on the weapon's sort
key: **14** takes the instant-use path (screen 0, mark `bUnk8`, deduct the cost),
anything else opens screen **5** without spending TU. Too little TU pushes the
out-of-TU message, clears the screen/latch/`nUnk40`, and sets mode 12.

**ConfirmFireBurst** (sample 16): clears the latch, publishes the multi-target
count into `nUnk40`, clears bit 0x40 on every multi-target cell, zeroes the count,
then enqueues the burst at the **first** multi-target cell's coordinates and kicks
the queue. The original dereferences `apMultiTargets[0]` unconditionally, so a zero
count is a NULL read; the port refuses instead but still clears the latch and
publishes the count, matching everything the original does before the dereference.

The burst enqueue reuses `TactState_EnqueueFireBurst`, so the mode's TU cost and the
enabled flag go in as the shot cost and fire argument � the same two table slots
`Ui_FillActionCosts` fills.

### Side lists and the soldier-list pass (done, same module)

`SideLists_RemoveUnit` (port of `Tact_RemoveUnitFromSideLists` @ 0x33148) and
`SideLists_MarkAllSoldiersSeen` (port of `Tact_MarkAllSoldiersSeen` @ 0x33bf0) live
in `src/pufo/game/pufo_roster.{h,c}`.

**Side-list removal** resolves against the `Map` type: `apXcomUnits` @ 0x15154 with
`nXcomUnitCount` @ 0x1511F (u8), `apEnemyUnits` @ 0x151CC with `nEnemyUnitCount`
@ 0x15124 (u8). It searches XCom **first** and the enemy list only on a miss, so a
unit present in both is removed from XCom only and its enemy entry survives � the
test pins that. Compaction copies entries down and skips the copy when the hit is at
the last index, where only the count changes.

**MarkAllSoldiersSeen** walks `apSoldiers` (2048 bytes = 512 pointers) and, for each
soldier with a live `pLinked1040` **and** non-zero `uHp`, increments the u16 at
`pLinked1040 + 250` and clears the link. Two details are reproduced: a **dead unit's
link is left in place** (the original does not clean it up), and the count is a u16
(IDA reads it with `LOWORD`). The port keeps the raw `+250` offset because the
linked structure is not modelled yet.

Deviations: the original reloads `nSoldierCount` from the context inside its loop;
the port reads it once, because nothing in the function changes it.

### Combat-menu item queries (done, same module)

`TactItem_GetHandSlot` @ 0x3e620 and `TactItem_GetEquipDelta` @ 0x3e69c are pure
look-ups over the action-id space, in `src/pufo/game/pufo_tactui.{c,h}`. Both were
transcribed branch by branch rather than turned into a table, because the id sets are
irregular and each gap value has a distinct meaning.

| id | hand slot | equip delta |
|---|---|---|
| 14 | 1 (left) | -1 |
| 15 | -1 (none) | -1 |
| 16 | 0 (right) | -1 |
| 17 | -1 (none) | **-2** |
| 18 | 1 (left) | -1 |
| 19 | 2 (both) | **-2** |
| 36 | **-2 (special)** | **-3** |
| 37 | -1 (none) | **1** |
| 38 | -1 (none) | **-3** |
| 39 | 1 (left) | -1 |
| anything else | 0 (right) | 0 |

Both share the shape "explicit ids first, then a default", and the two defaults are not
the same value for every range � which is exactly why the original's nesting is
mirrored rather than flattened. The test walks the table above plus unlisted ids on
both sides of the 19/20 boundary, and then sweeps ids 0..63 to assert no branch can
produce a value outside the declared sets.

### The 0xC350 tick handler (done, same module)

`GameTick_OnTimer` @ 0x274f0 is in `src/pufo/game/pufo_timer.{h,c}`. It is the one
place the per-frame decay, the tactical tick and the active-unit tick are driven
from, so it joins the timer set, the status stack and the action queue.

Verified control flow, as four early-outs followed by the tick body:

    if (bGameTickHold) return;                        // 0x5A
    if (bGeoscapeUi)   { geoscapeTick(); return; }    // 0x44
    if (uiSlot10 != 10 && uiSlot10 != NULL) return;   // apUiSlotsA[10] @ 0x88
    if (!bTactFlag)     return;                       // 0x00

    if (!busy) { decay every anTtl[i]; if (anTtl[0] <= 0) Shift(); }
    GameTick_Step();
    if (!(bPhaseActive && slot8Byte1)) TickActiveUnit();
    byte_138E74 = 0;

Three details that are easy to get wrong, all pinned by the test:

1. **The TTL decay is gated on `!busy`, but `GameTick_Step` is not.** The grid decay
   and the phase counter keep advancing while the game is busy; only the message
   countdown pauses.
2. **The shift tests `anTtl[0] <= 0` after the whole array has been decremented**, not
   per message, so at most one shift happens per tick. A stack whose second message
   also fell to 0 will shift again on the next tick, not twice in one.
3. **`TickActiveUnit` is skipped only when BOTH conditions hold** � phase active AND
   slot 8's second byte set. Either alone still ticks the unit.

`uiSlot10` is a pointer field compared against the integer 10 in the original, which
is why the port models it as an integer value rather than a pointer: the sentinel "no
screen" is the integer 0, not a null pointer check.

This is the first piece of the port that consumes the status stack, the action queue
and the timer set together, and the test drives all three through injected hooks.

### The small save chunks (done, same module)

Five more chunks in `src/pufo/game/pufo_save.{h,c}`, each a handful of field writes:

| chunk | original | payload |
|---|---|---|
| `gamestate` | `Save_WriteGameState` @ 0x44928 / `Save_ReadGameState` @ 0x44958 | 1 byte |
| `objbefore` | `Save_WriteObjBefore` @ 0x3f158 / `Save_ReadObjBefore` @ 0x3f0e8 | 2 bytes per object |
| `time` | `Save_WriteTime` @ 0x40204 / `Save_ReadTime` @ 0x41600 | 27 bytes |
| � | `Save_AssignRosterIndices` @ 0x42ea0 | no I/O; assigns ids |
| `strateg` | `Save_ReadStrategVer` @ 0x3f1c8 | 1 byte (`nSaveFmtVer`) |

**The `gamestate` chunk confirms the save/load asymmetry.** The writer takes its byte
from `pCtx[0]` (the tactical flag); the reader stores into `bGameState` @ 0x15821C, a
**different** cell. The port keeps both explicit � `Save_WriteGameState` takes the
flag, `Save_ReadGameState` fills a caller slot byte � rather than silently unifying
them, so the recorded deviation is visible in the API instead of hidden in an
implementation detail.

**The `objbefore` chunk serialises only 2 bytes per object**, the `uRecovScore` at
0xCA of the 208-byte `ObjectRecFull`. `uRecovCount` at 0xCC is NOT written, which the
test asserts by checking the count field is untouched after a round trip. The payload
is therefore exactly `2 * nObjectCount` bytes.

**The `time` chunk reaches its storage through the panel's line-pointer array**
(`gUiRootPanel.apLines[101..115]`), and the write order is NOT the slot order: 105,
106, 103, 102, 101, 114, 115, then a trailing word. Slots 105 and 106 are the ones the
port already documents as the game clock and the geoscape flag, which is why the port
models them as named fields rather than through the panel. Sizes are `u32, u8, u32,
u32, u32, u32, u32, u16` = 27 bytes.

**`Save_AssignRosterIndices` restarts its item counter at 0** rather than continuing
from the soldier count, so the two id spaces deliberately overlap. It is gated on
`bTactFlag` and does nothing without it. The two halves are independent: a NULL list
on one side does not stop the other.

### Special-9 confirmation and multi-target start (done, same module)

`TactAction_ConfirmSpecial9` @ 0x18260 and `Tact_BeginMultiTarget` @ 0x18380 are in
`src/pufo/game/pufo_tactui.{h,c}`.

**ConfirmSpecial9** (mode 9) has a four-condition refusal guard. All four must hold
before it refuses: not kneel/primed **and** `uHp == auStat[1]` (full HP) **and**
`bMorale == 100` **and** `uTuSpent != 0`. Breaking any one of them lets the action
through and sets the fire-target triple to the unit's **own cell**. The refusal path
pushes **two** status messages and leaves the target untouched � both pinned by the
test, which exercises each condition in isolation.

**BeginMultiTarget** (mode 7) reuses the fire modes' empty-weapon guard (`nAmmo == 0
&& bAmmoCap != 255`) and, on success, parks the cursor at its current cell, zeroes the
multi-target count, and sets the action latch to **3**, not 4. That distinction
matters: **latch 3 means "picking targets"** and latch 4 means "fire", so the two are
separate states in the same byte. The port names them `PUFO_ACTION_LATCH_TARGETING` and
`PUFO_ACTION_LATCH_FIRE` and the test asserts they differ.

`TactUiState` gained the `nFireTgtX/Y/Z` triple the confirm functions write.

### Camera offset writer (done, same module)

`MapView_SetCamOffset` @ 0x276f0 is in `src/pufo/game/pufo_tactctl.{h,c}`:

    if (bAdjust) h = 24*nLayerScratch + b - 20;  else h = b;
    v4     = a + 2*(h + 8);
    v5     = a/2 - h;
    bias   = (v5 <= 0) ? -8 : +8;        // exactly 0 takes the NEGATIVE bias
    out[0] = v4 / 32;                    // the two bytes at pCtx+0x4A40/0x4A41
    out[1] = (bias + v5) / -16;

Two details pinned by the test: the bias boundary is **`v5 <= 0`**, so `v5 == 0`
takes **-8**, not +8; and the second divisor is **-16**, kept as written.

`Map_IsoOffsetA/B` and `Map_CenterOffsetX/Y` were already implemented in
`pufo_mapcell.c` with a dimension-argument signature, so this module reuses them
rather than declaring a second, `MapGeom`-taking set � a first draft did exactly that
and the duplicate symbols were removed.

### Soldier searches (done, same module)

`Soldier_FindNextSmallByType` @ 0x47164 and `Soldier_CountSmallByType` @ 0x471f8 are
in `src/pufo/game/pufo_roster.{h,c}`. Both apply the same three ANDed conditions:

    bRankType == nType  &&  uHp != 0  &&  !Unit_bIsLarge(pUnit)

`uHp != 0` is the alive test and `!bIsLarge` excludes the tank-sized types, so a rank
match alone is not enough � the test exercises each condition in isolation with an
all-large roster and an all-dead roster.

`FindNextSmallByType` returns -1 for an `nStart` at or past the count, which is what
bounds the original's loop; the port reads the count once instead of reloading it each
iteration, because nothing in the function changes it.

## Enemy AI decision predicates (done)

Files: `src/pufo/game/pufo_enemyai.{h,c}`. Evidence:
`EnemyAi_bMultiTargetTypeMatch` @ 0x4b928, `EnemyAi_EnqueueRandomTurn` @ 0x4ab98.

### The unit-type table, decoded byte by byte

`byte_A7B80` @ 0xa7b80 is a 4-byte-stride table indexed by unit type. Two columns are
used; both were decoded from the 64 bytes in the image rather than inferred:

| type | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| flag0 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 1 | 1 | 0 | 0 | 1 | 0 | 1 | 0 | 0 |
| large | 0 | 0 | 0 | 0 | 1 | 1 | 1 | 1 | 1 | 0 | 0 | 1 | 0 | 1 | 0 | 0 |

`g_aUnitTypeFlag0` is new here; `g_aUnitTypeIsLarge` already existed and agreed with the
decode. The test re-states both columns independently, so a drift in the port's table
fails the test rather than silently changing the predicate.

### The predicate's asymmetry

`EnemyAi_bMultiTargetTypeMatch` switches on the **other** unit's `nMultiTarget` role:

| role | rule |
|---|---|
| 1 | `flag0 == 1` **AND** small |
| 2 | `flag0 == 0` **AND** small |
| 3 | `flag0 == 1` (size **not** tested) |
| 4 | `flag0 == 0` (size **not** tested) |
| anything else | true |

Roles 1 and 2 additionally require the unit to be SMALL; 3 and 4 test only the flag
column. Type 7 (`flag0 = 1`, large) and type 4 (`flag0 = 0`, large) are the clean
probes, and the test walks both against the size-testing and size-ignoring roles.

The role code is read from the **other** unit, not the candidate � the test sets the
candidate's own `nMultiTarget` to a value that would reject it and asserts the answer
does not change.

### Deviations

| # | Deviation | Why |
|---|---|---|
| 1 | An out-of-range `nUnitType` returns false | The original indexes the table with no bounds check; a read past it is undefined rather than defined behaviour |
| 2 | The unused first argument of the original is dropped | The body never reads it |
| 3 | `EnqueueRandomTurn`'s RNG draw and its enqueue+kick are injected | The RNG stream is the caller's, and the two queue calls always happen together |

### Facing scratch and the alien-base victory scan (done, same module)

`Map_InitFacingScratch` @ 0x26f14 and `Tact_ScanVictoryTiles` @ 0x33264 are in
`src/pufo/game/pufo_tactctl.{h,c}`.

**Facing scratch.** Fields resolved against `Map`: `nMapTileW` @ 0x30, `nMapTileH` @ 0x32,
`gap_52[0]` @ 0x34, `gap_52[1]` @ 0x35, `gap_52[2]` @ 0x36 (the stored facing), plus
`bTactFlag` @ 0x00. The whole body is guarded on the facing **changing**, so a repeat
call with the same facing is a complete no-op � including the tactical tile-size reset,
which the test proves by poisoning the size first and checking it survives. When the
facing IS new and the map is tactical: tile size resets to 240x320, `gap_52[0] = 9`, and
`gap_52[1] = 24*nLayerScratch/16 + 47`. The multiply-then-divide order is kept rather
than simplified, because truncation makes the operation order observable.

**Victory scan.** Two gates must both hold: a craft origin (`nCraftTileA[0] != 0`) and
`nMissionType == 4` (alien base). It then probes a **2x2 block** at the craft tile and
returns true at the first cell whose occupied-object list is empty. The probe order is
columns-within-a-row then the next row: (0,0), (1,0), (0,1), (1,1) � the test asserts
both the coordinates and the short-circuit (an empty third cell stops after three
probes).

The grid addressing is injected as a 2x2 probe because the original walks a 40-byte-stride
cell block at `pCtx + 0x193F0`, which is not the main `apCells` grid the rest of the port
models; resolving which named array that is would be its own investigation, and the
predicate's behaviour (the gates, the block shape and the order) is what matters here.

## Miscellaneous verified constructors

Two partial-clearing constructors, both reproduced as-is rather than "improved" into
a full zero:

| function | original | clears |
|---|---|---|
| `TactItem_ctor` | 0x3e718 | `pNext`, `bUnk8`, `bUnk7`, `nUnk16` � and nothing else |
| `TactBuf11408_ctor` | 0x54ff8 | only the u16 at `+6000` and the dword at `+9004` (`nModCount`) |

`TactItem_ctor` leaves `pObj`, `nIndex`, `nAmmo` and `nRefId` untouched, unlike every
other ctor in the port, which zero the whole struct. The test poisons all four and
asserts they survive, so a later "tidy-up" into a `memset` fails the test rather than
silently changing initialisation. `TactBuf11408_ctor` is the same idea: the node array
and the node count at `+0x1770` are deliberately left alone, which is why it is not a
`memset` either � the caller owns that state across a reload.

### Unit-type to glyph index (done, same module)

`Tact_MapTypeToGlyphIdx` @ 0x46664 is in `src/pufo/game/pufo_tactui.{h,c}`. Rules in
the original's evaluation order:

    type <= 3   -> type + 5
    type <  9   -> 18
    type == 16  -> 14
    type == 17  -> 13
    else        -> first match i in the four-byte probe table at gap_86272[22..25]
                   gives i + 9; no match gives type + 5

Two ordering details matter and are pinned by the test: types 16 and 17 are handled
**before** the probe even though they are above 9, and a probe match returns
`index + 9` � an index-relative result, not a type-relative one. A duplicate probe
entry resolves to the **first** match because the scan stops there, and the `+5`
fallback is **not** truncated to a byte (type 255 yields 260).


### Soldier name editing and overlay sprites (done)

`Unit_GetExtraSpr @ 0x2e59c`, `Map_BackspaceSelectedName @ 0x63138`,
`SoldierScreen_CopyNameToEdit @ 0x6318c` and `SoldierScreen_AppendNameGlyph @ 0x63208`
are recreated in `game/pufo_unit.{h,c}` and `game/pufo_soldiername.{h,c}`.

- `Unit_GetExtraSpr` returns `NULL` when the low byte of `anim.uFacingResetH`
  is zero and otherwise `Font_GetSprByIndex(&g_fontSys, anMc1Base[uExtraSprBase]
  + LOBYTE(uFacingResetH))`. The overlay-spray addresses are 12 (`LDRB
  [pUnit,#0x32]` for the facing byte) and 38 (`LDRH [pUnit,#0x26]` for the
  base), which resolve to `anim.uFacingResetH @ 0x32` and `uExtraSprBase @ 0x26`.
  The original's unused context argument is dropped.
- The three name functions take the `Soldier *` that the originals reach through
  `apSoldiers[gUiRootPanel.apLines[187]]`.
- `SoldierName_Backspace` returns `strlen(szName)` and clears `szName[len-1]`
  (the original writes `&uMissions + len + 1`, which is exactly `szName[len-1]`).
- `SoldierName_CopyToEdit` copies `szName` into the caller's buffer; the original
  also clears a one-byte scratch flag at `byte_186B88`, left to the caller here.
- `SoldierName_AppendGlyphAt` derives the glyph byte as
  `(x - 12) / 14 + 16 * ((y - 80) / 13 + 2)` with truncating division, refuses a
  zero-width glyph and appends otherwise.

Deviations: the original appends while `strlen < 40`, which lets a 40th
character overwrite the string terminator one byte past the 40-byte `szName`
field and into `bRankType`; the port instead requires the result plus terminator
to fit, so the maximum length is 39. The original's edit-buffer scratch
(`anLineAux[105]`) and the `byte_186B88` flag are passed in by the caller.

Covered by `tests/test_unit.c` (`TestExtraSpr`) and `tests/test_soldiername.c`.


### Tactical/geoscape directional input (done)

`Input_OnLeft @ 0x116c0`, `Input_OnRight @ 0x117b0`, `Input_OnUp @ 0x118b0`,
`Input_OnDown @ 0x119b0`, `Input_OnAction @ 0x1557c` and
`Input_OnMenuEscape @ 0x162ec` are recreated in `game/pufo_input.{h,c}` over a
`TactInputState` (the touched `pCtxBase` fields) and `TactInputHooks` (the
cross-subsystem effects).

- All four directional handlers share one precondition, `TactInput_CanScroll`:
  `bFireStatus0[2] == 0 && bTrajPad27[2] == 0 && bFireStatus0[1] == 0 &&
  !unk_157198->bQueueActive` (offsets 0x52, 0x1406d, 0x51 and the TactState byte
  at 0x111d6).
- With a clear geoscape flag and an active tactical flag: no UI screen scrolls
  the camera between `-((u8)gap_52[0] >> 1)` and `10*nMapDimX - (gap_52[0] >> 1)`
  horizontally (gap_52[1] >> 2 vertically); screen 1 scrolls the inventory or
  selects units; screen 2 moves `anCamWorldOff[0]`/row and changes the layer
  within 1..4 while playing sample 17.
- `Input_OnAction` sets the original's `byte_AD711` scratch flag and routes to
  `Input_OnConfirm` for screens 0..2/4, clears screens 6/12/15, else handles a
  map click. `Input_OnMenuEscape` opens screen 7 with sample 14, confirms
  screens 1/2/4 and 8/9/13, and clears 6/10/11/12/15.
- The camera-scroll ints are the dwords at 0x15130/0x15134 (IDA's `pad86320`),
  which the decompiler reaches through the byte alias `pad86309a[11]`/`[15]`;
  `pad86309a[19]` is `nLayerScratch @ 0x15138`.

Deviations: the cross-subsystem calls and `nullsub_3` are injected through
`TactInputHooks`, the inverse-camera sign in the original `--anCamWorldOff[0]`
is kept, and the `pfnOnConfirm` hook stands in for `Input_OnConfirm` (and, for
screens 8/9, `UiModal_End`). Covered by `tests/test_input.c`.


### Mute toggles and high-screen dismissal (done)

`Ui_ToggleMusicMute @ 0x19948` and `Ui_ToggleSfxMute @ 0x199ac` are recreated in
`game/pufo_menu.{h,c}` as `Menu_ToggleMusicMute` / `Menu_ToggleSfxMute` over the
existing `PufoOptions`: each flips `bMusicOff` (option byte +98 -> 0x150DA) or
`bSoundOff` (+99 -> 0x150DB), applies the matching volume and mirrors the new
value into its one-byte pad indicator (`byte_AD05A` / `byte_AD07A`). `UiScreen_DismissHigh @ 0x159ec`
is recreated in `game/pufo_input.{h,c}`: it clears the `byte_AD712` scratch flag,
ends the target modal for screens 8/9 and clears the screen, and for screen 13
runs the cancel handler while keeping the id.

`PadImage_ctor @ 0x11000` and `PadImage_FreeData @ 0x1100c` gained their exact
source citations in `render/pufo_render.{h,c}`.

Covered by `tests/test_menu.c` (`TestMuteToggles`) and `tests/test_input.c`
(`TestDismissHigh`).


### Screen back/cancel handlers (done)

`UiScreen_OnBack @ 0x15670` and `UiScreen_OnCancel @ 0x16200` are recreated in
`game/pufo_input.{h,c}`. `UiScreen_OnBack` clears `byte_AD711` and dispatches on
the screen: 8 runs `Tact_Shutdown(1) + App_QuitDisplay`, 9 runs `UiModal_End +
Tact_CheckBattleEnd(abort=1)`, 13 delegates to `UiScreen_OnCancel`.
`UiScreen_OnCancel` returns immediately when the tactical flag is clear, requires
the game-tick hold flag when a fire/queue gate is active, clears the modal screens
8/9 to 0, turns screen 13 into 19 or tears down to the geoscape
(`Tact_Shutdown(0) + Geoscape_InitUI(1)`), does the same for screen 19, and adopts
the low word of `apUiSlotsA[11]` for screens 20..21. The original's no-branch path
returns an uninitialised register; the port returns the current screen id
instead. Covered by `tests/test_input.c` (`TestOnBackCancel`).


### Cursor-direction animation state (done)

`TactHud_ComputeCursorDirState @ 0x1a7c8` and `TactHud_TickCursorDir @ 0x1e888`
are recreated in `game/pufo_cursordir.{h,c}` over a `CursorDirState` holding the
four `apUiSlotsA` cells the original uses: slot 8 bytes 2/3 (the direction bytes),
slot 9 (the per-tick step) and slot 10 (the countdown). The compute step maps
(byte3, byte2) -> (step, countdown) as (0,0)->(0,10), (0,1)->(-1,9), (1,0)->(1,1),
(1,1)->(0,0). The tick advances the countdown unless it is 0 or 10, shifts byte 2
into byte 3, writes byte 2 as 0 (when `!busy || hasTrackUnit || unk70115`) or 1
otherwise, and recomputes only when the old byte 2 differs from the value it
would take. Covered by `tests/test_cursordir.c`.


### Weapon clip reload (done)

`TactItem_bTryReloadClip @ 0x4b6a4` is recreated as `TactItem_TryReloadClip` in
`game/pufo_tactui.{h,c}`. When the magazine is empty, the weapon object's
`bAmmoCap` is not 255 and the owner has a spare clip (`bHumanoidInv != 0`), it
consumes one clip and refills the magazine from the clip object's own
`bAmmoCap`, looked up by the weapon object's first ammo-type id. It then returns
true when the magazine is non-empty, or when the weapon is unlimited, and false
otherwise. Because `TactItem.pObj` is the minimal `ObjectRec`, the two object
fields and the clip lookup are passed in. Covered by `tests/test_tactui.c`
(`TestTryReloadClip`).


### Throw and action-18 queue builders (done)

`TactState_EnqueueThrow @ 0x4b2f4` and `Tact_EnqueueActionOrStatus205 @ 0x4b420`
are recreated in `game/pufo_tactstate.{h,c}` as `TactState_EnqueueThrow` and
`TactState_EnqueueAction18`. Both run the strict out-of-TU test first (equality is
accepted), write nothing on failure, and append one entry: command 17 for the
throw (unit, X/Y/Z, weapon, single-byte argument, TU cost; the unit is faced first
through an injected `pfnFaceToward`) and command 18 for the action (unit, target
unit, weapon, TU cost, mode). Covered by `tests/test_tactstate.c`
(`TestThrowAndAction18`). Deviation: the original pushes the shared out-of-TU
status message internally; the port returns false and leaves that to the caller.


### Opportunity-fire scan (done)

`Unit_bScanOpportunityFire @ 0x48028` is recreated in `game/pufo_turn.{h,c}`. It
resets the context's opportunity-fire list (`nOppFireCount = 0`, `pScanUnit =
pUnit`), then walks the enemy roster when the unit is operative and the XCom
roster otherwise. Only shooters whose morale is strictly above 0x1E are offered
through the injected `pfnTryOpportunityFire` (the port of `Unit_bTryOpportunityFire
@ 0x47c38`), and the side's count is re-read after each offer to match the
original's post-casualty refresh. It returns whether any opportunity fire was
queued. Covered by `tests/test_turn.c` (`TestScanOpportunityFire`).


### Cursor placement and look mode (done)

`MapCursor_SetPos @ 0x27014` and `MapView_EnterLookMode @ 0x163d4` are recreated
in `game/pufo_tactctl.{h,c}`.

- `MapCursor_SetPos` runs when `!bBusy` or the target is not a valid layer-0 cell.
  It clears `bFlags57` bit 3 on the current cursor cell and, for a large selected
  unit, its 2x2 footprint on all four layers; then, when the target is inside
  `0..10*dim-1` on both axes, stores it and sets the same bits, marking the
  footprint only when the unit is large AND `gap_2[1]` is clear. The clear phase
  deliberately omits the `gap_2[1]` condition; only the set phase checks it.
- `MapView_EnterLookMode` plays sample 16, sets the screen to 2, centres the camera
  on half the tile size and derives the two world-offset words from the
  camera-scroll values at 0x15130/0x15134 plus `nTrajFront0[4]/[5]` minus 30/40.

Covered by `tests/test_tactctl.c` (`TestMapCursorSetPos`, `TestEnterLookMode`),
which drives the cell writes through a real 4-layer grid.


### Animation pose derivation (done)

`Unit_UpdateAnimPose @ 0x4978c` is recreated in `game/pufo_unit.{h,c}`. A dead
unit (flag 0x20) forces pose 18 and masks `bFlags` to 0xE0. An aiming unit (flag 4)
gets pose 15/16/17 by kneel/climb. Otherwise the walk/kneel/climb flags give a
base pose 0..4, then a held item whose object has list priority 1 or sort key
22/24 adds 10 (the left hand is checked first), any other hand-held item adds 5,
and no item leaves the base. The two hand object fields are passed in because
`TactItem.pObj` is the minimal `ObjectRec`. Covered by `tests/test_unit.c`
(`TestAnimPose`).


### Exit-direction wall probe (done)

`Unit_CanStepExitDir @ 0x36d68` is recreated in `game/pufo_unit.{h,c}`. It takes
the unit's overlaid path triple (`nMultiTarget` X, `gap_1B` Y, `anim.nPathZ`
layer), offsets it by the direction's `g_aStepDir` step, bounds-checks it, then
scans that direction's four (dx, dy, kind) wall-probe triples (the 96-dword table
transcribed from the decompilation). A kind-2 probe tests the cell's wall-A
sprite, any other kind its wall-B sprite, only when the blank/blocking test
passes, and a sprite whose byte 0x2F (`gap_2f[0]`) is set blocks the step. It
returns true when no wall was found. Cells come from an injected
`MapCellLookup`. Covered by `tests/test_unit.c` (`TestCanStepExitDir`).


### Per-cell isometric glyph draw (done)

`MapView_DrawCellFloorGlyph @ 0x1c904`, `MapView_DrawCellOccupantGlyph @ 0x1b45c`,
`MapView_SetWallPalFromSlotA @ 0x1c7dc` and `MapView_SetWallPalFromSlotB @ 0x1c870`
are recreated in `game/pufo_isodraw.{h,c}` over a minimal `IsoDrawCtx`
(pCell 0x00, nDrawX 0x04, nDrawY 0x08, bWallPalIdx 0x64, nPal 0x6c) plus the
`IsoAnimGlobals` (nAnimPhase, nAnimPhaseScratch[0]). The floor/occupant helpers
draw the sprite at `nDrawY - sprite gap_25` with the phase as the glyph index. The
wall helpers pick the palette byte from the cell flags when the sprite's
`bBlocksMove` (0x2a) is set: bit 0 -> the scratch byte, bit 2 -> 7, else 0; with
`bBlocksMove` clear the phase is used. Covered by `tests/test_isodraw.c`.


### Armour TU warning (done)

`Unit_bArmorTuWarn @ 0x4e254` is recreated in `game/pufo_unit.{h,c}`. It returns
false when the `byte_AC81A` option disables the warning (option bytes passed in),
picks the armour-TU slot from `byte_AC83A`/`byte_AC85A` (1/2, else 0), takes the
larger hand cost `abArmorTu[slot] * auStat[0] / 100` (falling back to
`abArmorTu[1]` when the slot value is 0), and warns when the unit can just afford
that cost now but spending `nTuCost` would drop it below. The two status messages
are pushed through an injected callback. A two-handed weapon is ignored by a
non-large unit. Covered by `tests/test_unit.c` (`TestArmorTuWarn`).


### Cell-occupant spotting (done)

`Unit_SpotCellOccupant @ 0x48138` is recreated in `game/pufo_unit.{h,c}`. It
returns false only when the cell has no occupant. Otherwise, after the viewer
side/status rules accept the occupant, the occupant is appended to the viewer's
multi-target list (skipping duplicates) and, while the world is busy, to the
spotted list, or, when idle, flagged through its `bUnk148`; an operative viewer
also focuses the camera on a newly spotted selected unit and sets
`nTrajFront0[1036]`. The Soldier `multiOrAnim` union (animation view vs the
30-entry multi-target pointer array) is now modelled explicitly. Covered by
`tests/test_unit.c` (`TestSpotCellOccupant`).


### Best-scoring soldier selection (done)

`Soldier_PickBestScoreAmong @ 0x46fe0` is recreated in `game/pufo_roster.{h,c}`.
It scores eight roster slots as `5 * (uKills + 2 * uMissions)` (a -1 slot index is
skipped) and returns the highest, writing the winning score and slot index through
two output pointers. The original leaves the score uninitialised when slot 0 is
-1; the port starts from 0 and documents the deviation. Covered by
`tests/test_roster.c` (`TestPickBestScore`).


### Research save chunk (done)

`Save_WriteResearch @ 0x3f558` and `Save_ReadResearch @ 0x406e8` are recreated in
`game/pufo_save.{h,c}` over `SaveResearchState`: four opaque header blobs
(0x900, 0x900, 0x90, 0x90 bytes) followed by 144 `ResearchTopic` records. Each
record is written field by field in the original's order, which is not the
in-memory field order, so the chunk is 4896 + 144*34 = 9792 bytes. Covered by
`tests/test_save.c` (`TestResearchChunk`), which round-trips the first, middle and
last topics.


### Smap save chunk (done)

`Save_WriteSmap @ 0x3fa28` (and a matching reader) are recreated in
`game/pufo_save.{h,c}` over `SaveSmapState`: five header dwords, 200 `SmapSite`
records written field by field (740 on-disk bytes per 742-byte site; `gap_519` and
`gap_741` are not serialised), and 5 + 8 + 8 trailing bytes. The chunk is
20 + 200*740 + 21 = 148041 bytes. Covered by `tests/test_save.c` (`TestSmapChunk`).


### Product save chunk (done)

`Save_WriteProduct @ 0x3f4f4` and `Save_ReadProduct @ 0x4062c` are recreated in
`game/pufo_save.{h,c}` over one contiguous block of `PUFO_PRODUCT_BLOCK_BYTES` = 17152 bytes.
The product table is three parallel arrays that sit back to back in the original, and the
chunk reaches them by a fixed offset from the first:

```
A  32-bit, 1600 entries, 6400 bytes, block +0      (0x159E9C)
B  32-bit, 1600 entries, 6400 bytes, block +6400   (0x15B79C)
C  16-bit, 1600 entries, 3200 bytes, block +12800  (0x15D09C)
tail                       1152 bytes, block +16000 (0x15DD1C)
```

Writing is always the new layout -- 6400, 6400, 3200 -- and the tail is never written. All
three writes run; the original returns only the last result, so a failed middle write does
not stop the chunk.

Reading branches on the save format version byte at 0x18655C, and the important part is what
does NOT change: the offsets stay 0 / 6400 / 12800 / 16000 in both branches, only the sizes
shrink.

```
version >= 3: A 6400, B 6400, then the shared tail read puts C (3200) at +12800
version <  3: A 4608, B 4608, C 2304, then the tail read puts 1152 bytes at +16000
```

So an old save fills entries 0..1151 of each array and leaves 1152..1599 holding whatever was
there before the load. The two branches are the same layout read with different widths, not
two layouts. The test proves it by pre-filling the read buffer with a sentinel and asserting
the gap stays sentinel. The `| 4` in the original's last address is the compiler folding the
four-byte gap between `&anLineAux[149]` and `&anLineAux[150]`: `12800|4` lands on block +12800
and `16000|4` on block +16000. Covered by `tests/test_save.c` (`TestProductChunk`, 31 checks).


### Bases save chunk (done)

`Save_WriteBases @ 0x3f1fc` and `Save_ReadBases @ 0x402c0` are recreated in `game/pufo_save.{h,c}`
over `SaveBasesState`. Unlike the product chunk this one is not a contiguous table: it is a flat
sequence of blocks and field-by-field records, and the port keeps the pieces separate.

```
   288  block at 0x159768
  1152  block at 0x1598DC
     1  base count, 0x159D80 (IDA: gUiRootPanel.anLineAux[79])
     1  0x159D81
     1  0x159D82
     4  0x159D90
   8/4  0x159DBC   -- 8 written, but only 4 READ when the version byte at 0x18655C is below 3
     4  0x1598D8
   0/1  0x158848   -- written always, READ only when version >= 3
    17  BaseFacFlag[i].bFlag20, i = 0..16, stride 28
     9  0x18655D
   488  8 facility records, 61 bytes each
  then per base, while the count byte is > 0 as a signed char:
    16  BaseRec: bSlot, bUnk4, nStoresTotal, nUnk8, nUnk12, bUnk17, bUnk16
    2n  one u16 per object: auPerBaseVal[baseIndex]
```

Header 1974 bytes, then 16 + 2*objectCount per base.

**The facility record is serialised in an order that is not its memory order.** In memory
`BaseFacility` is 64 bytes laid out `bType, gap_1, uUnk2, uUnk4, abUnk6[20], abName[20], gap_46,
nUnk48, nUnkDemolishState, nUnkBuildState, nFacType`. On disk the nine writes run `abName, bType,
nUnkBuildState, abUnk6, nFacType, nUnk48, uUnk2, uUnk4, nUnkDemolishState` -- 61 bytes. `gap_1` at
+0x01 and `gap_46` at +0x2e are never written.

**The per-base column is the loop index, not `BaseRec.bSlot`.** 0x3F3CC seeds a byte offset at 42
and 0x3F4C0 adds 2 per base, so base `i` writes `ObjectRecFull.auPerBaseVal[i]` of every object.
42 is 0x2a, the offset of that array.

**The two version gates are a real format difference, not a compatibility shim.** The writer always
emits 8 bytes at 0x159DBC and always emits the 0x158848 byte; the reader consumes 4 and skips it
when the version byte is below 3. Reading a modern chunk with the old branch shifts everything
after the gate five bytes early -- the test asserts the first place-flag byte lands on the 0x1598D8
block.

**The reader frees before it reads.** 0x402D0..0x40310 deletes `nOldBaseCount` `BaseRec` pointers
using the count the game held BEFORE the chunk is read, then reads the new count and allocates
that many fresh records.

`BaseRec_ctor @ 0x55650` is recreated as `BaseRec_Ctor` in `game/pufo_ship.{h,c}`. It zeroes
`auPerBaseVal[nSlotIndex]` in every object record, then sets `nStoresTotal = 0`, `bUnk4 =` the
global byte at 0x186558, `bSlot = nSlotIndex`. The index is the signed byte at 0x159D80 at the
moment the ctor runs -- in the save reader that is the base **count** just read, so loading a
three-base save zeroes column 3 of every object, one past the last column the writer produced.
In that path all three field writes are immediately overwritten by the values from disk, so the
column zeroing is the ctor's only live effect there.

**Recorded deviation.** The original has no bound check on that index. With the maximum eight bases
the index is 8, and 2*8 bytes past `auPerBaseVal` (0x2a) lands on `aItemQty[0]` at 0x3a, so the
ctor zeroes the first stock count of every item in the game. The port clamps the index instead of
reproducing that out-of-range write; the test asserts `aItemQty[0]` survives. Everything else in
the chunk is reproduced byte for byte. Covered by `tests/test_save.c` (`TestBasesChunk`, 47 checks).


### Ships save chunk and `Ship_ctor` (done)

`Save_WriteShips @ 0x3f6dc` and `Save_ReadShips @ 0x4086c` are recreated in `game/pufo_save.{h,c}`
over the port's existing 68-byte `Ship`. One byte ship count, then per ship 61 bytes of fields and
one u16 per object: `1 + n * (61 + 2 * objectCount)`.

The field order is neither the struct order nor any grouping -- nFuel, bHomeBase, nSlotIdx,
bCraftType, bWeaponL, uRangeRemain, bHangarX, bHangarY, anCargoQty[16], bNeedsSupply, bCanDogfight,
uUnk4, uFuelMax, uRangeMax, bCrewSlots, bType, bAtBase, bWeaponsHot, bAmmo27, bAmmo30, bAmmo29,
bAmmo25, bAmmo26, bRefueling, bWeaponR, bId, uFuelCap, uUnk14, uRangeRate, bIntercepting,
nStrategIdx, bContactMsg.

**This chunk writes the other object column.** 0x3F720 seeds the per-object offset at 58 and
0x3FA08 adds 2 per ship, and 58 is 0x3a -- `ObjectRecFull.aItemQty`. So the bases chunk fills the
`auPerBaseVal` columns and this one fills the `aItemQty` columns, both indexed by loop position.

**The two readers free in opposite orders.** `Save_ReadBases` frees first and calls FindChunk
after; `Save_ReadShips` calls FindChunk at 0x4087C and frees at 0x40888, still before the count is
read, so both free with the count they already had.

**A loaded interceptor gets its range max overwritten.** 0x40C24 tests the `bCraftType` that was
just read from disk and, when it is 0, stores 10000 into the u16 at `Ship + 0x0A` -- `uRangeMax`.
The value saved in the file is discarded. The test saves 1234 for a type-0 craft and asserts 10000
comes back, while a type-3 craft keeps its saved 1235.

`Ship_ctor @ 0x45c20` is recreated as `Ship_Ctor` in `game/pufo_ship.{h,c}`. If the `bSkip` byte is
set the ctor does nothing at all. Otherwise it clears the ammo bytes, cargo counts, contact and
intercept state, and `uRangeRemain`; 0x45C80 stores the u16 1 at `&bAtBase`, which sets `bAtBase`
to 1 and `bRefueling` to 0 in one write; it takes `nSlotIdx` from the CURRENT ship count and then
zeroes `aItemQty[nSlotIdx]` in every object record.

`bType` comes from a per-craft-type counter, one byte each at 0x18653C .. 0x186540, incremented on
each construction. A craft type above 4 leaves `bType` at 1 and sets no range or fuel field at all,
while still clearing the cargo column. The craft table:

| type | crew | dogfight | rangeMax | rangeRate | needsSupply | fuelMax | fuelCap |
|---|---|---|---|---|---|---|---|
| 0 interceptor | 0 | 1 | 10000 | 20 | 0 | 150 | 760 |
| 1 | 1 | 1 | 30000 | 2 | 1 | 800 | 3100 |
| 2 | 2 | 1 | 60000 | 2 | 1 | 1200 | 5400 |
| 3 | 2 | 0 | 1000 | 20 | 0 | 100 | 3000 |
| 4 | 2 | 0 | 20000 | 2 | 1 | 500 | 4500 |

Type 2's `uRangeMax` is written as `-5536` into a u16, which is 60000. `nFuel` starts equal to
`uFuelMax` and `uUnk14` is 2 for every type that reaches the table.

**Recorded deviation.** `aItemQty[nSlotIdx]` is written with no bound check; `aItemQty` has 52
entries. The port clamps. Covered by `tests/test_save.c` (`TestShipsChunk`, 41 checks).


### `Save_AllocFlobjs` (done)

`Save_AllocFlobjs @ 0x431ac` is recreated in `game/pufo_save.{h,c}`. It finds the `flobjs` chunk,
reads the tactical item count, and allocates that many `TactItem` records (`new(0x18)` plus
`TactItem_ctor @ 0x3e718`) into the caller's array. Unlike the bases and ships readers it frees
nothing first.

**The count read is 1 byte or 4 bytes wide**, decided by the dword at `Map + 0x118` (0x113AD0):
4 when it is non-zero, 1 when it is zero. Its only two writers in the IDB are `Game_StartNew`
(0x94554) and `Base_StripAllCraftCargo` (0x69a38), and neither is ported, so the meaning of that
flag is **not established** -- the port takes it as an explicit argument instead of guessing. What
is established is the consequence: with the narrow width the original stores one byte into a
4-byte little-endian global, so the upper three bytes of the count keep whatever they had. The
port reproduces that and the test asserts `0x123400` becomes `0x123402`.

Covered by `tests/test_save.c` (`TestAllocFlobjs`, 24 checks).


### The flobjs chunk body (done)

`Save_WriteFlobjs @ 0x42f48` and `Save_ReadFlobjs @ 0x43258` are recreated in `game/pufo_save.{h,c}`.
17 bytes per item: the object **index**, nIndex(2), nAmmo(1), bUnk7(1), bUnk8(1), the next item's
pool index(4), the linked soldier's `nSaveId`(4).

**Both links are stored as indices with 0x7FFF as the null sentinel.** The next-item link stores
`pNext->nRefId`, which is the pool index `Save_AssignRosterIndices @ 0x42ea0` gave that item, and
the reader resolves it back through the same array. The soldier link stores `nSaveId`, which is the
roster index, and the reader indexes `apSoldiers` with it directly.

**The object index comes from a linear search, and a miss is saved as an out-of-range index.** The
loop at 0x42FB0..0x42FE4 leaves the index equal to `nObjectCount` when nothing matches, and the
original writes that and lets the reader dereference `apObjects[that]`. The port refuses instead.

**A load reads the count twice.** `Save_AllocFlobjs` allocates the records and `Save_ReadFlobjs`
fills them, and both call `FindChunk` and read the count through the same 1-or-4 width gate. The
writer always stores 4 bytes.

Covered by `tests/test_save.c` (`TestFlobjsChunk`, 20 checks).


### The map chunk (done)

`Save_WriteMap @ 0x438e0` and `Save_ReadMap @ 0x433d8` are recreated in `game/pufo_save.{h,c}`.
The chunk is two dimension bytes and then **43 bytes for every cell** the map dimensions cover:
`10*nMapDimY` rows by `10*nMapDimX` columns by 4 layers, so a 6x6 map covers the whole grid.

**The grid is a fixed 240-row array, not a sized one.** Both functions build the cell address
with the same ARM sequence, `ADD R3, layerCounter, rowCounter` then `MUL R2, R3, #0xF0` then
`LDR R4, [base, #0x15338]`, where the layer counter is 0, 60, 120 or 180. That is apCells
@ 0x15338: 60 pointers per row, 14400 bytes per layer, 240 rows total, whatever the map size is.
The port reaches the same cells through the `MapCellLookup` the other map algorithms already use.

**Coordinates are derived, not stored.** The reader writes nX, nY and nZ from its own loop
counters and clears nUnk20, bUnk24 and gap_25.

**A load wipes the shot-trace flag as a side effect.** Restoring bFlags29 bit 4 uses the mask
`0x8F`, which also clears bits 5 and 6. Bit 5 is the flag `Map_ClearShotTraceFlags` sweeps before
every arc trace, so loading a save clears it on every cell without anyone asking.

**What is not serialised**, proven by pre-filling a cell with ones after the write and reading it
back: `nFlags32` and `nFlags40` store only 7 bits each (3 plus 4) and keep whatever bits 7 and up
already held in memory; `bFlags57[0]` stores only bits 2 and 3 and leaves bits 0 and 1 alone;
`bFlags44` loses bit 1 on the way in whatever it held.

Covered by `tests/test_save.c` (`TestMapChunk`, 40 checks).


### The four roster chunks (done)

`Save_WriteTeamUnits @ 0x43c84` and `Save_ReadTeamUnits @ 0x44020` are recreated in
`game/pufo_save.{h,c}`. One function writes **four** chunks and one reads all four back:

```
teamunits   1 byte count, the selected unit's id, then 30 ids   = 125 bytes
aliens      1 byte count, then 30 ids                           = 121
diedsolds   4 byte count, then 60 ids                           = 244
hiddensold  4 byte count, then 30 ids                           = 124
```

Every id is the soldier's `nSaveId`, or `0x7FFF` for an empty slot.

**The counts are not the same width.** teamunits and aliens store one byte (`Map.nXcomUnitCount`
@ 0x1511F and `nEnemyUnitCount` @ 0x15124 are bytes); diedsolds and hiddensold store four
(`nCivilianUnitCount` @ 0x15120 is a dword).

**Only teamunits stores a selection, and it stores it before the roster** — the second field of
the chunk (0x43CE0), not after any list.

**The writer gates each slot on `index < count AND slot != NULL`, the reader ignores the count it
just read.** A roster slot past the count is written as `0x7FFF` even when the array holds a
pointer there, and the reader always consumes the full 30 / 30 / 60 / 30. A stale pointer beyond
the count is therefore dropped by a save/load round trip. `TestTeamUnitsChunk` proves this with a
unit parked at `apXcom[5]` with a count of 3.

**The hidden reserve is not part of the Map block.** It is `unk_138DF0`, the 30 dwords that start
exactly where Map ends (`0x1139B8 + 152632 = 0x138DF0`), which is why Hex-Rays reaches it as
`&v10[1].gap_TrajRest[45476]`.

Covered by `tests/test_save.c` (`TestTeamUnitsChunk`, 28 checks).


### The tactical session block (write side done)

`Save_WriteTGameData @ 0x446a8` is recreated in `game/pufo_save.{h,c}`. One call writes **eight**
chunks in this order: `tgamedata`, `teamunits`, `aliens`, `diedsolds`, `hiddensold`, `map`,
`v124.dat`. It calls the roster, flobjs and map writers above rather than duplicating them, which
is what fixes the chunk order of a tactical save.

The `tgamedata` payload is 23 fields in a fixed order, 43 bytes. The offsets come from the IDB's
`Map` struct gaps, and two of them are already-known fields, which is what pins the arithmetic:
`gap_86272[20]` is 0x15114, the port's `nMapLayers`, and `gap_86301[1]` is 0x1517E, the
light-enable gate `Map_RefreshCellMeta` reads.

**The dword at Map+0x15116 is four sprite-set ids.** It is written as four bytes, and the reader
consumes each byte as a sprite-set index: for every byte that is not 23 it loads the set named
`xcom_<byte>` through `Font_LoadSpr` and then rebuilds the font index. 23 means "no set for this
slot". This is the only place a mission's four faction sprite sets are recorded.

**The three craft-tile fields are dwords in memory and are written one byte each.**

`v124.dat` is two dwords at `unk_138E6C` and `unk_138E70`. On the read side they are optional: if
the chunk is missing the original rolls them as `Game_RandU16(40) + 10` and `Game_RandU16(20)`.

`TestTGameDataWrite` compares the written payload byte for byte against a 43-byte expectation
built by hand from the decompilation, so the field order is checked, not assumed.

NOT ported, and deliberately not cited: `Save_ReadTGameData` (0x44248). It reads the 43 fields
back, reloads the four sprite sets, calls the three roster/flobjs/map readers, then clears about
thirty tactical state fields (`nMultiTargetCount`, `nAnimPhase`, `bPlayerInputEnabled` to 1, the
fire-status bytes, `bEnemyTurnActive`, `bBattleEnded`, most of `apUiSlotsA`, `nUiScreen`) and sets
`apUiSlotsA[4]` to 200 and `apUiSlotsA[10]` to 10.

Covered by `tests/test_save.c` (`TestTGameDataWrite`, 18 checks).


### The smap chunk reader is not a mirror of the writer (done)

`Save_ReadSmap @ 0x40c4c` is recreated in `game/pufo_save.{h,c}`. An earlier note in the source
claimed the original had no separate reader for this chunk. That was wrong: the reader exists and
it does something the writer does not.

After reading `nMode`, `nScrollX` and `nScrollY` into the viewport at 0x1863E8, and only when
`unk_15821C` is zero, it switches on the mode it just read and **restores the whole zoom level**:
the tile grid and layer globals are swapped to that level's set, the world pair becomes 800x400,
600x300 or 472x236, and `nMapW`/`nMapH` are taken from that level's grid PadImage. A mode outside
0..2 restores nothing — the switch has a `default: break`, which the test exercises with a stored
mode of 0x11111111.

**No scroll rescale and no vertical clamp run on a load**, unlike `Smap_ZoomIn @ 0x81e44`: the
stored scroll is restored exactly as saved. The port reuses `Smap_ApplyZoomSet`, which is the same
piece both zoom functions apply, rather than writing a second copy.

The tail order is also not ascending: 5 bytes go to `unk_161F40`, the next 8 to `unk_162000`, and
the last 8 to `unk_161FF0`, which sits **below** `unk_162000`.

Covered by `tests/test_save.c` (`TestSmapChunk`, 17 new checks on top of the 268-check round trip).


### The rmp chunk (done)

`Save_ReadRmp @ 0x43ee0` is recreated in `game/pufo_save.{h,c}`. There is no writer for this
chunk: it records the random-map modules a mission was built from, and `Tact_InitMission @ 0x3c550`
is the other caller.

A count, then six blocks, then one `Map_LoadRmp` call per module. The six blocks are 60 dwords
each except the names, which are 60 * 20 = 1200 bytes. **The order they are read in is not the
order the loader takes them in:**

```
read 1 -> nTileX   read 2 -> nTileY   read 3 -> nSizeX   read 4 -> nSizeY
read 5 -> the 20-byte names           read 6 -> nLevel
```

and the call is `Map_LoadRmp(rmp, nTileX[i], nTileY[i], nSizeX[i], nSizeY[i], name + 20*i,
nLevel[i])`. Getting that order wrong silently builds the map with sizes where levels should be,
so the port takes the loader as a callback and the test records the arguments instead of trusting
the round trip.

A zero or negative count loads nothing. Deviation: the original indexes its six fixed 60-entry
arrays with no bound check; the port refuses a count above 60.

Covered by `tests/test_save.c` (`TestReadRmp`, 25 checks).


### The save/load slot list (done)

`SaveSlot_FormatList @ 0x45904` is recreated in `game/pufo_save.{h,c}`. It builds the seven lines
of the save and the load screens, and it is a reader of seven **separate** files: for slot `n` it
opens `<dir>sav<n>.sav`, so the list is built by opening each save in turn.

The path is `<dir> + "sav" + itoa(n) + ".sav"` (0x45944..0x45994). The same 296-byte buffer is then
reused as the scratch for every `itoa` after the file is open; only the first one uses a separate
52-byte buffer, because the path is still needed at that point.

A missing file gives the whole line from one text record:

```
aTextRecs[205 * 1074] = "Game not saved"
```

A present file is opened and the line is built from the `time` chunk: read 4 bytes into
`GameTime.nTotalSec`, `GameTime_Decode`, then

```
(day    < 10 ? "0" : "")  + day
(month  >= 10 ? "." : ".0") + month
"." + year
(hour   >= 10 ? " " : " 0") + hour
(minute >= 10 ? ":" : ":0") + minute
```

The separator doubles as the pad character, so a single-digit month renders `.03` and a
single-digit hour renders ` 09`. The prefix copied at 0x459E4 is `aTextRecs[205 * 1800]`, which is
**past the end of TEXTS.DAT** (344202 bytes = 1721 records), so in this data set it is an empty
string and only terminates the line. That is the same out-of-range record
`UiControl_LoadPads @ 0x1a604` reads as an empty pad name.

Then the `gamestate` chunk is read into `bGameState @ 0x15821C`, the flag that says a tactical
mission is stored in the file. When it is set the line gets `aTextRecs[205 * 1075]`, which is
`" (mission)"`, and only if a `mcnt` chunk is present, `" "` plus that byte as a decimal number.

Two side effects:

- the decoded date is written into the **live clock**. Only `nTotalSec` is restored at 0x45C10; year,
  month, day, hour and minute are left holding the values decoded from the last save file that was
  inspected, which is the last slot whose file existed, not the last slot.
- `bGameState @ 0x15821C` is overwritten from the last file that had a `gamestate` chunk.

`FindChunk` and `Read` results are ignored by the original: a save without a `time` chunk simply
formats whatever `nTotalSec` already held.

Deviation: the original concatenates with unbounded `strcat` into a 40-byte line; the port bounds
every append to 39 characters plus the terminator.

Covered by `tests/test_save.c` (`TestSlotList`, 21 checks).


### The strateg chunk (done)

`Save_WriteStrateg @ 0x3fbb4` and `Save_ReadStrateg @ 0x40f4c` are recreated in
`game/pufo_save.{h,c}`. The chunk is a **134-byte header** followed by the strateg list at
**113 bytes per item**.

The header is 38 writes in a fixed order that is not address order. Most of these globals have no
known meaning yet, so `StrategSaveState` names each field for the address it comes from. The known
ones:

- `bSaveVersion` at 0x18655C — the save format version. It is saved **here**, and
  `Save_ReadProduct`, `Save_ReadBases` and `Save_ReadShips` all branch on it.
- `nStrategCount` at 0x186530 — the item count. It is **not** the PtrArray's own count field: the
  list at `strategList @ 0x186534` keeps its count at 0x186534 and its item array at 0x186538, so
  0x186530 is a separate global that mirrors the count and is also the loop bound in both
  directions.
- `abTypeCounters[5]` at 0x18653C..0x186540 — one byte per craft type. `Ship_ctor @ 0x45c20` bumps
  them and stores the result in `Ship.bType`, so saving them is what lets `bType` survive a load.
- `nSelectedShipIdx` at 0x187054.

Two header entries are used as **pointers**, not values: 0x1865A8 and 0x186594 hold addresses that
`DatFile_Write` is handed directly, so the saved value is what they point at.

The block 0x186568..0x1865BC is 22 dwords and 21 are written, in a scattered column-by-column
order. **0x18657C is the one header dword a save does not carry.**

The 33 item fields are written in an order that is neither address order nor size order, and the
entry at `+0` (`nType`) is written twelfth into the record rather than first. The port keeps that
order exactly, because it is what the byte layout on disk is.

Reader differences from the writer, all of them load-bearing:

- `PtrArray_DeleteAll` runs **before** the header is read, so a failed load still empties the
  strateg list. `FindChunk`'s result is not checked either.
- the version byte is read into a scratch and **discarded**. The per-item gate at 0x41554 then tests
  the build's own 0x18655C, not the value that came from the file. A save written by an older build
  is read with the **current** build's record size.
- `nTargetX` and `nTargetY` are only read when that version is above 2, so an old record is 105
  bytes, not 113. Proven directly: with the version at 2 and two 113-byte records on disk, the
  second item's `bActive` comes out as the low byte of the **first** item's `nTargetX`.
- the count is a global the reader only overwrites when the read succeeds. With a stale non-zero
  count and no strateg chunk at all, the original still allocates that many items and reads nothing
  into them, so the list comes back full of uninitialised records.

Deviation: the original reads and writes through fixed globals and never checks a `DatFile` result;
the port takes the state and the list as arguments and returns false when the chunk is missing,
while still emptying the list the way the original does.

Covered by `tests/test_save.c` (`TestStrategChunk`, 96 checks).


### Camera bounds (done)

`Tact_GetCameraBounds @ 0x3331c` is recreated in `game/pufo_tactctl.{h,c}`. With
the craft flag clear it returns the whole map `(0,0)-(10*dimX,10*dimY)`. For the
alien-base mission type it returns the bottom three-tile strip (`X = 10*(dimX-3)`
to `+30`, `Y = 10*dimY-10` to `10*dimY`). Otherwise it centres on the craft tile
and widens by craft type: type 1 adds 20 to X and 10 to Y; types 0 and 2 add 10 to
X and 20 to Y; other types leave the box. The 0/2 shared block and the type-1 block
were resolved from the disassembly at 0x333c4/0x333f4. Covered by
`tests/test_tactctl.c` (`TestCameraBounds`).


### Base-screen scratch helpers (done)

`Set_byte_159E88_4 @ 0x6b6b0`, `Set_byte_159E88_3_SetUiY @ 0x6b674`,
`Cache_ObjPlus0xBD8_To_dword_186BE8 @ 0x7afec` and
`PlaySample7_SetFlags_Cache0xBD8 @ 0x73328` are recreated in
`game/pufo_baseui.{h,c}` over a `ScreenScratch`: the first two set the panel
scratch byte to 4 and (3 plus the two controls' y = 120/142); the last two cache a
source `nUnk3032` into `dword_186BE8` / `unk_160C74`, the second also playing sample
7 and setting `unk_160C80`/clearing `byte_187080`. Covered by `tests/test_baseui.c`.


### Strategic screen list paging (done)

`BaseFacScreen_PanelPrev @ 0x589cc`, `BaseFacScreen_PanelNext @ 0x589dc`,
`SoldierScreen_PanelPrev @ 0x61874` and `SoldierScreen_PanelNext @ 0x6189c` are
one-liners over the two scroll-cursor movers that were already recreated in
`ui/pufo_panel.c`: `UiPanel_ScrollNext` (`IncDwordIfInRange_AtPlus0xBD4 @ 0x3f00c`,
`if (nUnk3028 < nLineCount - nStyle) ++nUnk3028`) and `UiPanel_ScrollPrev`
(`DecDwordIfPos_AtPlus0xBD4 @ 0x3f040`, `if (nUnk3028 > 0) --nUnk3028`). Neither wraps.

`BaseFacScreen_Panel*` moves the single panel the original points at through
`unk_15886C`. `SoldierScreen_Panel*` moves two panels, the pointers held in
`gUiRootPanel.apLines[186]` and `gUiRootPanel.anLineAux[106]`, in that order, and the
second call is the value the original returns. The limit is `nLineCount - nStyle`, so a
panel whose visible row count equals its line count can never page -- that is what
`nStyle` is for. Covered by `tests/test_baseui.c` (`TestPanelPaging`, 9 checks).


### Screen selection caches and the base info OK button (done)

`BuySellScreen_SyncSelection @ 0x76138`, `Transfer_CacheSelIndex @ 0x96ab0` and
`ManufScreen_SyncSelection @ 0x6dd24` are the same seven instructions: load a
screen-object pointer, read the dword at `+0xBD8` (IDA names the field `nUnk3032`, and
3032 == 0xBD8), store it into one global, return it. `0x76138` and `0x96ab0` are byte
for byte identical and both write `dword_186BE8`, the cache
`Cache_ObjPlus0xBD8_To_dword_186BE8 @ 0x7afec` already owns, so the port forwards both
to `BaseUi_Cache186BE8`. `0x6dd24` reads `unk_15EE38` instead of `unk_161A1C` and writes
`gUiRootPanel.anLineAux[147]` (`0x159E90`), which the port takes as a pointer. Nothing
clamps the index.

`BaseInfoScreen_OnOk @ 0x557c8` sets `byte_1870F8` to 1, copies the SIGNED byte at
`byte_1870F4` into `byte_158834` (`0x557DC` uses `LDRSB`, so a negative index stays
negative), and returns it. `0x1870F4` is the base the psi screen selected --
`Geoscape_CloseUI @ 0x8b94c` case 19 fills it from `Base_CalcPsiLabCap @ 0x6598c` -- and
`0x158834` is the base the geoscape screens work on. The two writers of `0x158834`
disagree in width: `0x557E0` stores a byte, case 19 stores a dword. Covered by
`tests/test_baseui.c` (`TestSelectionCaches`, `TestBaseInfoOk`, 14 checks).


### Screen openers, close thunks, and the second paging pair (done)

`PsiScreen_OnOk @ 0x716a8` is byte for byte the same eight instructions as
`BaseInfoScreen_OnOk @ 0x557c8`, `LDRSB` included, so the port keeps one implementation
and two names.

`BuySellScreen_ListPanelPrev @ 0x79a20` and `ListPanelNext @ 0x79a48` are the same
two-panel shape as the soldier screen, over `unk_161EDC` first and `unk_161A3C` second.

Six openers are recreated over one shared body and a `ScreenInitHooks`:
`ManufScreen_Open @ 0x614cc` (id 9), `TransferScreen_Open @ 0x614fc` (10),
`BaseInfoScreen_Open @ 0x6167c` (3), `BuySellScreen_OpenBuy @ 0x61794` (11),
`BuySellScreen_OpenSell @ 0x617c4` (12), `BaseFacScreen_Open @ 0x61844` (7), and
`BasePlaneScreen_Open @ 0x617f4` (6, the hangar). Two facts
come only from the disassembly. `Sound_Play` takes a second argument, 4 (`MOV R1, #4` at
0x614D4 and in every opener). And the order of the Init and the screen id is not uniform:
Manuf (0x614EC), Transfer (0x6151C) and BaseInfo (0x6169C) write the id **after** the
Init, while BuySell (0x617AC, 0x617DC) and BaseFac (0x6185C) write it **before**. An Init
that reaches the close dispatcher therefore sees a different screen id depending on which
opener ran, and the test captures that by reading the id from inside the Init hook.

Seven close thunks -- `BaseInfoScreen_CloseThunk @ 0x557bc`,
`BaseFacScreen_CloseThunkA @ 0x589c0`, `BaseFacScreen_CloseThunk @ 0x59b54`,
`SoldierScreen_CloseThunk @ 0x64ae4`, `ManufScreen_CloseThunk @ 0x6e3d4`,
`ResearchScreen_CloseThunk @ 0x72ffc`, `BuySellScreen_CloseThunk @ 0x75154` -- are not
seven behaviours. Each is `LDR R0, =0x1864F8` and `B Geoscape_CloseUI`: one tail call
with one argument, written seven times so every widget can hold its own function pointer.
`NewBaseScreen_CloseThunk @ 0x615b0` is the odd one: it plays sample 17 first.
`Geoscape_CloseUI @ 0x8b94c` itself stays a hook (~40 unported callees).

`BaseAtScreen_Open @ 0x588cc` is not an opener of the same family. It plays no sample and sets
no screen id: it positions one `UiControl` and installs its click handler, then tail-calls the
screen's own update. The ARM pins the shape -- `LDRH [R3,#4]`, `LSR #1`, `RSB R1, R1, #0x78`,
`STRH [R3]` gives `x = 120 - (uWidth >> 1)` stored as 16 bits, so the control is centred on
x = 120; `MOV R3, #0x120`, `ORR R3, R3, #2`, `STRH [R1,#2]` gives y = 290 built as `0x120|2`;
`STR [R1,#0xC]` installs the click handler. `MOV R0, R1` at 0x588D0 drops the first argument --
the mode is the second one, a char, which is the same trick `OptionsScreen_OpenWithSound @ 0x6dd04`
plays in the other direction. The control pointer itself lives at 0x186BA4, so the port takes the
control as an argument. Its callees, `BaseAtScreen_CloseThunk` (0x577c0) and `BaseAtScreen_Update`
(0x57808), are unported and are named without a citation.

`OptionsScreen_ReturnToTitle @ 0x6b46c` clears the panel scratch byte, closes the
geoscape screen, frees every geoscape widget with mode 1, and returns the result of
`GameUI_InitStartup @ 0x38a28`. Covered by `tests/test_baseui.c`
(`TestBuySellPaging`, `TestScreenOpeners`, `TestCloseThunks`, `TestTitleReturn`, 40 checks).


### Twelve copies of one close call, three-panel paging, the options radio groups (done)

Five more thunks join the seven above: `FundingScreen_CloseThunk @ 0x5abd8`,
`FundingScreen_ExitThunk @ 0x5c078`, `Geoscape_CloseUI_ThunkA @ 0x838d0`,
`Geoscape_CloseUI_ThunkB @ 0x96020`, `Geoscape_CloseUI_ThunkC @ 0x9a8c0`. Twelve widget
handlers now reach the same dispatcher, and none of them is a separate behaviour.
`Geoscape_OnMapClick_Thunk @ 0x857e8` is the same idea for the map: it drops the four
arguments the widget framework passes and tail-calls `Geoscape_OnMapClick @ 0x8b4a8`,
which is not ported, so it stays a hook.

`ResearchScreen_PanelPrev @ 0x72e30` and `PanelNext @ 0x72e68` page **three** panels,
`unk_160C68`, `unk_15EEFC`, `unk_15EF00`, in that order.

`OptionsScreen_ClearMusicButtons @ 0x6b994` and `OptionsScreen_ClearSfxButtons @ 0x6b9d4`
write zero into byte `+10` of six widget pointers held in `gUiRootPanel.anLineAux[128..133]`
and `anLineAux[134..139]`. Byte `+10` is `UiControl.nState`, the same selected/radio byte
the bank widgets use, so these are "deselect the whole radio group", not a field wipe. The
value returned is the sixth widget pointer itself (`0x6B9C4` loads it before the last
store), not a status code. One implementation, two names.

`StrategList_RegisterAtexit @ 0x838a0` is `return atexit(StrategList_Clear);` -- the
original registers its own cleanup with the C runtime, possible because its strategic list
is a global and its clear takes no arguments. The port's `StrategList_Clear` takes the
list, so it registers a thunk over the list handed in.

Deliberately **not ported**: `BaseGrid_RegisterAtexit @ 0x614b4` and its thunks
`BaseGrid_Atexit_nullsub5 @ 0x614a8` / `BaseGrid_Atexit_nullsub6 @ 0x614c0`. They register
`nullsub_5` and `nullsub_6`, empty compiler-generated stubs, so they carry no behaviour.
Covered by `tests/test_baseui.c` (`TestMoreCloseThunks`, `TestResearchPaging`,
`TestOptionsRadioClear`, `TestAtexitRegistration`, 21 checks).


### Craft loadout weapon selection (done)

`BasePlaneScreen_SelectWeapon25..30 @ 0x79b20/0x79b64/0x79ba8/0x79bec/0x79c30/0x79c74` are
six copies of thirteen instructions, ported as one function `BasePlaneScreen_SelectWeapon`
with the index as an argument, in the new `game/pufo_plane.{h,c}`.

The loadout screen carries two weapon panels, and each panel only takes the new weapon when
its own gate is set: `unk_161F18` gates `unk_161F10` and `unk_161F1C` gates `unk_161F14`. A
closed panel keeps its old selection. The swap runs either way -- 0x79B54 is a `B`, a true
tail call to `BasePlaneScreen_ApplyWeaponSwap @ 0x79748`, so its result is what these handlers
return.

The widths come only from the disassembly: the gates are read with `LDRB` and the selection
is written with `STRBNE`, so the selected weapon index is a **byte**.

### The loadout swap and its ammo stores (done)

`BasePlaneScreen_ApplyWeaponSwap @ 0x79748`, `BasePlane_UnloadWeaponStores @ 0x790e8` and
`BasePlane_LoadWeaponStores @ 0x793e0`, all in `game/pufo_plane.{h,c}`.

**A weapon and its ammo are separate object records.** The swap moves `apObjects[weapon]`
through cargo and base stores, while the two store functions move `apObjects[52..55]` and
`apObjects[105]`. That is easy to miss and it changes what the call order means: the new
weapon leaves base stores before its ammo is loaded, but since those are different records the
load does not see a reduced stock.

The store functions take a **weapon index**, not a pointer. The decompiler types the argument
`BaseRec *` but every test in both functions is on the low byte, and the callers pass
`Ship.bWeaponL` or `Ship.bWeaponR` straight in.

| weapon | ammo record | load |
|---|---|---|
| 25 | `apObjects[52]` | 10 |
| 26 | `apObjects[53]` | 3 |
| 27 | `apObjects[54]` | 2 |
| 29 | `apObjects[105]` | 10 |
| 30 | `apObjects[55]` | 2 |

Weapon 29 counts **rounds of 10 per base item**, so the unload multiplies by 10 and the load
divides by 10 on both paths; a leftover round from the divide is dropped, not carried.

Unload is one rule written out as five branches with the comparison flipped between them:
take `min(stock, load)` from base stores into the craft's carried byte. Load is the reverse,
and a mode decides how much comes back. Panel A's gate gives mode 2 unless this weapon is the
craft's **right**-hand weapon, where it drops to 1; panel B's gate gives mode 2 unless it is
the **left**-hand weapon, where it drops to 1 — and the second test **overwrites** the first,
so with both gates only panel B's test survives. Mode 2 leaves one load on the craft and
returns only the excess; any other mode returns everything and zeroes the byte.

In the swap, the order that is load-bearing is the **weapon-byte assignment at 0x798BC, which
comes last**. The cargo removal at 0x79838 re-reads the weapon byte, so assigning it any
earlier removes the new weapon from the craft instead of the old one. When the craft carries
nothing, the two base-store calls for the old weapon and the cargo removal are absent, and the
base-store call for the new weapon moves to the end, after the load.

Neither store function's return value is a status: Load returns the weapon index on some paths
and the selected craft index on others, and no caller reads either. Covered by
`tests/test_plane.c` (71 checks).

### Adding an intercept mission (done)

`BasePlaneScreen_AddInterceptMission @ 0x7de14`, ported in `game/pufo_plane.{h,c}`.

**IDA's signature for this function was wrong.** It declared no arguments. The disassembly opens
with `MOV R3, R1, LSL #24` then `MOV R3, R3, ASR #24` — a sign-extend of R1's low byte — and R1
is then used twice: as the index into the mission-site table (`MUL R2, R3, #0x18`, a 24-byte
stride) and again unchanged as the `bCraftClass` argument. Applying
`int __fastcall BasePlaneScreen_AddInterceptMission(char bCraftClass)` to the IDB made the
argument appear in the pseudocode, which is the proof.

The table is 24-byte records at `0x1865C8`, indexed by craft class. `x` is the **signed** 16-bit
value at record +0 and `y` at +2; both loads are `LDRSH`. The remaining 20 bytes are untouched
here. The table lives in `.bss`, so it is filled at run time and carries no static data.

It adds a strateg item with `nType 3`, `bActive 1`, `bVisible 1` and `nId` from the mission-id
counter `dword_18654C`. Because `nType` is 3, that id lands in `nSiteId` and `nDisplayId` becomes
-1. It then re-reads the list count, sets the new last item's `nTimer` to **2880**, and stores
`count-1` into `0x186BE4` — the index of the item it just created — reading the count a third
time for that store rather than reusing it.

Finally it post-increments the mission-id counter and returns the **incremented** value, so the id
the item carries is the value the counter held *before* the call. Tested on both sides.

Deviation: the original indexes with the sign-extended char and never bounds it, so a negative
craft class reads before the table. The port refuses, and refusing leaves the id counter
untouched.

This and `Base_FindFacilityCell @ 0x6ae54` clear both named dependencies of
`Geoscape_SpawnMissionSite @ 0x7e49c`.

### The UFO mission machine (done)

`Geoscape_SpawnMissionSite @ 0x7e49c`, 4460 bytes, ported in `game/pufo_strateg.c`. The largest
single geoscape function ported so far, and it was only portable once every callee was in place.

**The outer switch is on the argument, not on the item.** `-1` advances the mission already stored
in `pUfo->nMissionSub`. `0..7` starts that subtype. Anything else returns `nMissionSub + 1` having
changed nothing.

The advance path gives every subtype the same three gates in the same order:

| gate | what it does |
|---|---|
| `nPhase > 3` | pick a target and travel to it, with `bC` set to the subtype |
| `nPhase == 3` | teleport onto the target if it lies inside the current viewport, set a dwell, drop to phase 2 |
| `nPhase < 3` and `nTimer == 0` | run the landing effect, then move to a fresh random point |

The dwells are **200, 200, 720, 2880, 60, 1 and 720** for subtypes 1 through 7. Subtype 0 has no
target gate and no dwell at all: it travels to a random point and decrements whenever `nPhase > 0`,
skipping the `nTimer` test the shared tail applies.

What actually differs between subtypes is **where the target comes from**:

- **1, 2, 4** re-roll until `Tilemap_GetAt` is not 255, so they always land on dry ground.
- **3, 5** take a random entry from the 56-record mission-site table at `0x1865C8`. Subtype 5 also
  remembers which entry, because its landing effect reports that index to the radar message.
- **6** picks a random base and wants one with **no** free cell of facility kind 10. It retries up
  to 20 times and on the twentieth gives up on bases entirely, travelling to a random point and
  returning **before the gates run**.
- **7** collects every active type-0 item into a 100-entry stack array and picks one at random.

The landing effects: subtype 4 drops an alien site (undiscovered when `rand(0x14)+1 <= 16`, else
discovered with line 60 and a clock pause) and stamps the new item with the UFO's race and target
index. Subtype 5 raises an intercept mission, stamps the same two fields, appends line 16, arms
`nRadarPulseTimer` to 250 and pauses. Subtype 6 opens the base-contact screen and sets the screen
id to 20.

The shared tail returns `nPhase` as it was **before** the decrement, and marks `bDone` only when
`nPhase` lands on 0 and the craft is not visible.

Covered by `tests/test_strateg.c` (41 checks).


### Geoscape screen openers (done)

`Ui_OpenScreen18 @ 0x84e9c`, `Ui_CloseScreen18 @ 0x84ecc` and
`Ui_OpenFunding @ 0x85164` are recreated in `game/pufo_baseui.{h,c}` over
`GeoscapeScreenState` and `GeoscapeHooks`. Each plays sample 17, sets the geoscape
screen id (18/18/15 -- note the close function also sets 18) and calls the matching
options/funding screen. `Ui_CloseScreen18` passes `anLineAux[119]` to the options
open. Covered by `tests/test_baseui.c` (`TestGeoscapeScreens`).


### Inventory burden (done)

`Unit_RecalcInventoryStat @ 0x49ef0` is recreated in `game/pufo_unit.{h,c}`, and
the Soldier inventory block is now modelled explicitly (`pHandL`, `pHandR`,
`InvPocketPair aPockets[4]`, `apBackpack[8]`, `apBelt[9]` and their meta arrays)
instead of opaque bytes. The burden starts at 1 and adds each hand/backpack/belt/
pocket item's object weight, plus the ammo object's weight for a loaded weapon
(`nAmmo != 0`, `bAmmoCap != 255`) that is not two-handed. The full-Record object
fields are injected through `InvStatHooks`. Covered by `tests/test_unit.c`
(`TestRecalcInventory`).


### Door-action queue builder (done)

`Map_QueueDoorAction @ 0x2bd00` is recreated in `game/pufo_tactstate.{h,c}`. Given
the unit's cell, the clicked cell and the footprint corner selector, it picks the
door wall sprite (wall-A for corners 2/6, wall-B for 0/4), then either queues an
open (cmd 2) plus a door (cmd 3) pair when the sprite's byte 0x2F is set, or -- when
the sprite blocks movement and the target cell is unflagged -- scans the facing
direction up to 3 cells back and 7 forward for the door span and queues an open plus
a step-door (cmd 4) pair carrying the far cell. Covered by
`tests/test_tactstate.c` (`TestQueueDoorAction`).


### Force-no-drag toggle (done)

`TactHud_ToggleFlag128A9C @ 0x19d94` is recreated in `game/pufo_menu.{h,c}` as
`Menu_ToggleForceNoDrag` over the existing `PufoOptions.bForceNoDrag` (+108). It
flips the flag and writes its pad indicator as the INVERSE of the new value
(turning ON writes 0, turning OFF writes 1), unlike the mute toggles. Covered by
`tests/test_menu.c` (`TestMuteToggles`).


## Geoscape clock and tick chain (done)

| Created | Original evidence |
|---|---|
| `game/pufo_clock.{h,c}` | `GameTime_Decode @ 0x5cfd4`, `GameTime_Reset @ 0x5c56c`, `Geoscape_AdvanceTime @ 0x5ce40`, month table `dword_AB270 @ 0xab270` |
| `game/pufo_ship.{h,c}` | `Ship_TickRangeRecover @ 0x45f80`, `Ship_TickRefuelAtBase @ 0x45f04`, `Ship_TickRangeRecoverConsume @ 0x45fc8`, `BaseRec_SubItemQty @ 0x5571c`, `BaseRec_ctor @ 0x55650` |
| `game/pufo_strateg.{h,c}` | `StrategList_UpdateMotion @ 0x80fd8`, `StrategList_UpdateDetect @ 0x81948`, `Strateg_CalcDetect @ 0x81378`, `StrategList_CountActive @ 0x81a2c`, `StrategList_FindByShipId @ 0x5a9b4`, `Ship_ClearExpiredIntercepts @ 0x8128c` |
| `game/pufo_geoscape.{h,c}` | `Geoscape_TickShips @ 0x5d4e4`, `Geoscape_TickHourly @ 0x5d1bc`, `Geoscape_TickBases @ 0x5d3bc` |

Tests: `tests/test_clock.c`, `tests/test_ship.c`, `tests/test_strateg.c`,
`tests/test_geoscape.c`.

### The pause byte is one global, not a per-module flag

`GameTime_Pause @ 0x5d6ac` is two instructions: `LOBYTE(gUiRootPanel.apLines[106])
= 1`. That same byte is what gates the minute block of `Geoscape_TickShips`
(`LOBYTE(apLines[106]) == 0` at 0x5d574), so a contact message stops the geoscape
on the next minute tick.

Every geoscape status message that announces something the player must react to is
followed by it, and the ones that do not are the ones that only inform:

| Message | Posted at | Pauses |
|---|---|---|
| 10 radar contact | `Strateg_CalcDetect` 0x818f4, 0x817c0, 0x815a4 | yes (0x818fc, 0x817c8, 0x815c4) |
| 13 intercept lost | `Ship_ClearExpiredIntercepts` 0x81330 | yes (0x81340) |
| 15 ship contact | `Geoscape_TickShips` 0x5d65c | yes (0x5d664) |
| 43 low supply | `Ship_TickRangeRecoverConsume` 0x4608c | **no** |

The port first modelled the pause as part of the `pfnContactMsg` hook, which is
wrong for 43 and invisible to the port's own clock. It now writes
`time.bPaused` directly at the three pausing sites, and `GeoscapeWorld` carries a
`GameTime *pTime` so the strateg layer reaches the same byte the tick owns —
matching the original, where both read and write one global.

### Ship range recovery has two mutually exclusive paths

`Ship_TickRangeRecover @ 0x45f80` (per minute) and `Ship_TickRangeRecoverConsume
@ 0x45fc8` (per hour) both require `bAtBase`, both require `uRangeRemain <
uRangeMax`, and they are selected by `bNeedsSupply`: the plain path adds
`uRangeRate`, the supply path adds `1000 * uRangeRate` and drains the base.
Neither runs away from base, which is why `test_geoscape` needs two ships to pin
both the recovery and the contact path in one minute tick.

### Gate slot and stock slot are different indices

`Ship_TickRangeRecoverConsume` reads the gate as
`apObjects[105]->auPerBaseVal[bHomeBase]` (0x4601c) but subtracts through
`BaseRec_SubItemQty @ 0x5571c`, which writes
`auPerBaseVal[apBases[bHomeBase]->bSlot]`. Two expressions, not one.

`BaseRec_ctor @ 0x55650` closes the question: it sets
`pBase->bSlot = gUiRootPanel.anLineAux[79]`, the base count at the moment the
base is created, and the same value is used as the per-object slot to zero. So
`apBases[i]->bSlot == i` for every base the game creates, the two indices
coincide in a real game, and the low-supply branch is reachable. With a
hypothetical `bSlot != bHomeBase` the gate value never moves, so the warning at
0x46060 can never fire — `test_ship.c` pins both cases.

The low-supply test itself is `stock < rate` after the subtraction, so a base
holding exactly `2 * rate` is the boundary: `20 > 10` consumes down to 10 and
`10 < 10` is false, no warning.

### Corrections made while writing these tests

Recorded because they were wrong before they were checked, not because they are
subtle:

- `test_geoscape` left `StrategList.nCount` at 0 while configuring items, so the
  mission prune, the intercept sweep and the `CountActive < 2` fallback roll all
  saw an empty list. The assertions passed by accident.
- `test_geoscape` expected the minute counter to advance while paused. The
  original increments inside the `apLines[106] == 0` block, so a paused geoscape
  does not count minutes at all.
- `test_geoscape` expected range recovery on a ship with `bAtBase == 0`.
- `test_ship` used `auPerBaseVal[2]` for both the gate and the stock while
  `bHomeBase` was 0, so the gate read an empty slot and nothing was consumed.
- `test_ship` reused a ship already clamped to `uRangeMax` for the low-supply
  case; the `uRangeRemain < uRangeMax` gate blocked the consume and the warning
  never ran.
- `test_clock.c` did not build: an unused `Check` helper under `/WX`.


### Next game-core targets, and the one blocker

The largest remaining `tact` gaps are the action processors and the shot pipeline:
`Map_BuildMissionTerrain @ 0x22e10` (10676 B), `Tact_ProcessFireAction @ 0x50adc`
(8192), `Tact_ProcessMoveAction @ 0x4f3f0` (5868), `Tact_CheckBattleEnd @ 0x34fbc`
(5748), `Tact_ProcessFallAction @ 0x52adc` (5716), `Tact_KillUnit @ 0x4e430` (3748),
`Combat_ResolveShot @ 0x2f12c` (2828), the throw/arc trio `Tact_SimulateThrowArc @
0x3102c`, `Tact_TraceProjectileArc @ 0x2fc38`, `Tact_ResolveThrowLanding @ 0x31a18`,
and `Combat_SplashDamageCells @ 0x2e7d8` (2388).

The blocker is not the logic, it is the layout of the aim block inside `Map`.
`Tact_ComputeAimVector @ 0x30368` writes the shot state into unnamed `Map` gaps:
the 11-entry per-weapon table from `byte_A9218[11 * nFlags]` into `Map+0x13FD8`
(81880) as 11 dwords, then the two endpoints at +44/+48/+52 and +56/+60/+64
(81924..81944), the yaw into a named double, and the pitch into `Map+81968` or
`Map+81960` depending on whether the shot is vertical. It also writes
`MapCell.bFlags29 |= 0x20` on the origin cell and `bFireStatus0[1] = 1`,
`bFireStatus0[2] = 0` (Map+81, Map+82 — already named in `pufo_input.h`).

One reference, `*(_DWORD *)pMap->gap_82056 = nFlags` at 0x305c0, does not resolve
against the current `Map` type (152632 bytes), so the IDB has to be fixed before
this cluster is ported: name the aim block as a real sub-struct of `Map`, give the
11-entry table at `byte_A9218` its own type, and re-check the `gap_82056` write.
Doing that in the IDB first is cheaper than reverse-engineering the offsets from
disassembly for each of the six functions that read the block.

## Ballistics block: aim vector, shot arc, throw solver

Recreated as `game/pufo_ballist.{h,c}`, tested by `tests/test_ballist.c`
(37 checks, 0 failures).

| Original | Recreated |
|---|---|
| `Tact_ComputeAimVector` @ `0x30368` | `Tact_ComputeAimVector`, `Aim_ComputeAngles` |
| `Tact_TraceProjectileArc` @ `0x2fc38` | `Tact_TraceProjectileArc` |
| `Tact_SimulateThrowArc` @ `0x3102c` | `Tact_SimulateThrowArc`, `Throw_HeightAt` |
| `atAimRowTable` @ `0xA9218` | `g_atAimRowTable` |

### The aim block is resolved, the blocker is gone

Every write in `Tact_ComputeAimVector` is an immediate offset from the Map base
register, so the block layout came out of the disassembly directly:

```
0x13FD8  11 dwords from atAimRowTable[11 * nFlags + i]
0x14004/08/0C  origin X/Y/Z      0x14010/14/18  target X/Y/Z
0x14020  dTravel    0x14028  dAngleYaw    0x14030  dAnglePitch
0x14038  dTravelSave
0x14060/64/68  trajectory end X/Y/Z (dwords)
0x14078  arc denominator (double)
0x14080/84  the bound double, held as two dwords
0x14088  aim row id
```

Two earlier readings were wrong and are corrected:

- `*(_DWORD *)pMap->gap_82056 = nFlags` at `0x305c0` is not a pointer write. The
  ARM is `MOV R3, #0x14088; STR R0, [R4,R3]` - a plain dword at `Map+0x14088`.
- `dAngleYaw` is at `0x14028` and `dAnglePitch` at `0x14030`. The decompiler's
  `v30 = 81968 / 81960` pair looked like two pitch fields; the yaw store at
  `0x14028` in the non-vertical branch and the random yaw store in the vertical
  branch are the same field.
- `padFire2` (8 bytes at `0x14060`) plus the four bytes at `0x14068` are three
  dwords: end X, end Y, end Z. `MapView_DrawUnitHeightGlyph` @ `0x1c240` proves
  it - it reads them as `% 16`, `% 16`, `% 12`, i.e. voxel remainders.

The float constants, decoded as lo/hi dword pairs, are 1.0 (arc step), 1/16 and
1/12 (voxel to tile/layer), 0.1 (random yaw scale), 9.18 (throw gravity),
1/(2*9.18) (range form), 0.05 (search step scale), 0.001 (search base) and
1.57075 - the original's own half-turn constant, not pi/2.

### The arc stops on a floor hit only

`Tact_TraceProjectileArc` tests the occupancy result truncated to one byte:

```c
if ((u8)Map_TraceVoxelOccupancy(...) != 0) { /* keep walking */ }
else { record target; stop; }
```

A clear voxel returns `LOS_HIT_NONE` (-1), which is 0xFF as a byte, so the walk
continues. Only a result of 0 (`LOS_HIT_FLOOR`) stops it and records the target.
Wall, ceiling and occupant hits do not stop the arc. Recorded as the original's
rule, not corrected.

The abort test is `ltd(x, 160 * nMapDimX) == 0`, which is `x >= limit`, not
`x < limit`; the first port had it inverted and the test caught it.

### The throw solver

`Tact_SimulateThrowArc` searches 19 angles `theta = base + step * 0.05 * i`
starting at `base = 0.001`, `step = 1.57075`. Launch speed is fixed by the
height term `v = (dz > 0 ? dz + 3 : 3)` through `v0^2 = 2 * 9.18 * v`, the
candidate is rejected when its range `v0^2 * sin(2*theta) / (2*9.18)` exceeds
the horizontal distance, and it is accepted when the trajectory height at that
distance lands in `[dz, dz+1]`. When two consecutive angles straddle `dz` the
search restarts with `base = that angle` and `step *= 0.05`.

The straddle conditions were settled against the disassembly, because
Hex-Rays renders them wrongly. It sets
`v29 = __PAIR64__(nDz, (int)hNext)` and then compares against
`SLODWORD(v29)`, which reads as `dz > dz` and makes both refinement branches
look dead. The ARM at `0x318f8..0x31928` is unambiguous (`R1` = dz, `R8` =
height at the current angle, `R0` = height at the next angle, `R3` = dz + 1):

```
0x318f8  CMP R1, R8   BGT  0x3191C     ; dz > h0  -> second test
0x31904  CMP R8, R3   BLE  0x319D4     ; h0 <= dz+1 -> leave the loop
0x3190c  CMP R1, R8   BGE  0x3191C     ; dz >= h0 -> second test
0x31914  CMP R1, R0   BGT  0x31984     ; dz > h1  -> refine
0x3191c  CMP R1, R0   BGE  0x3192C     ; dz >= h1 -> continue
0x31924  CMP R1, R8   BGT  0x31984     ; dz > h0  -> refine
```

So both guards are live and compare dz against the two heights, exactly as the
port has them. `0x31394 CMP R7, #0x13 / BGE 0x319D4` confirms the 19-iteration
bound, and `0x3161c MOVS R3, R0 / BEQ 0x3192C` confirms that a failed range
test skips the height test rather than ending the search.

`test_ballist` pins three solved arcs: 32 voxels flat gives pitch 0.31515 and
denominator 23.9437733; 48 voxels flat gives 31.6606068; 16,16,12 gives
19.3139508. Those numbers come from the search itself, so a change in the
angle set or the constants moves them.

## Next target brief: Combat_SplashDamageCells @ 0x2e7d8 (read once, port next turn)

Decompiled and worked through; recorded here so the next turn does not have to
re-read it. Not ported yet.

Signature `Combat_SplashDamageCells(Map *pMap, int x, int y, int z, int a5, int a6)`.
`a5` and `pMap->nFireArg6` are the same kind of object - a weapon record - read
at `+200` (u16 weapon class) and `+182` (u16 TU cost). Classes 50 and 15 are the
two special cases.

Shape: a 41 x 41 cell sweep centred on (x, y) - `cellX = (x - 20) + ix`,
`cellY = (y - 20) + iy`, `ix, iy` in 0..40, fixed layer `z`. Cells outside
`Map_IsValidCell` are skipped. Each cell is then walked over its four sprite
slots, `slot = 0 floor, 1 wallA, 2 wallB, 3 ceiling`.

Per-(cell, slot) hit record: `Map.gap_TrajRest` (Map+0x4ebc) indexed as
`4 * (41 * iy + ix) + 4 * slot` dwords, i.e. 4 dwords per record. Only two are
read: dword 0 (`power`, u16) and dword 3 (`unitPower`, u16). A zero `power`
skips the record. Thresholds: `power > 0x14` gates damage and flag mutation,
`power > 0x46` gates one flag mutation, `unitPower > 0x41` and `unitPower > 0xA`
gate the two special weapon classes.

Behaviour groups, in the order the original applies them:

1. `a6 == 2` (and class not 50/15): when `power >= sprite[25]`, sets
   `nFlags32 = (nFlags32 & ~7) | 4`, `nFlags40 = (nFlags40 & ~7) | 2`, clears
   bit 0 of `bFlags36`. Then, unless `nFlags32 & 7 == 4`, randomly rewrites the
   low 3 bits of `nFlags32` with `Game_RandU16(3)` and of `nFlags40` with
   `Game_RandU16(2) + 2`.
2. `a6 == 1` and slot is floor or ceiling: same `nFlags32 | 4`, random
   `nFlags40` low 3 bits from `Game_RandU16(2) + 2`, sets bit 0 of `bFlags36`.
3. Slot 0 and `power > 0x14` (class not 50/15): `Unit_ApplyCellDamage @ 0x2e5d0`.
4. Slot 3 with an occupant that is not already hit (`gap_433[0] == 0`) and class
   not 50/15: damage = `unitPower - bUnk248 * (1 - ready / 100)` where `ready`
   is `abReady252[2]` for `a6 == 1`, `abReady252[0]` for `a6 == 0` (skipped when
   the unit is `pSelUnit`) or `a6 == 2`, and the previous cell's value for other
   `a6`. Then a random reduction `Game_RandU16(damage / 4)` is subtracted,
   clamped at 0. Armour `nArmorPoints` absorbs first (clamped to 0, remainder
   kept); the rest comes off `uHp` and is added to `nUnk436`, or `uHp` goes to 0.
   Stun: when `damage >= auStat[1] >> 1` and the unit is not large,
   `bKneelOrPrimed += (damage - (auStat[1] >> 1)) / 10 + 1`. Then `gap_433[0] = 1`
   and `Unit_EnqueueIfApSpent @ 0x4a3d4` with 1.
5. `a6 != 0` and `power > 0x14` and (class != 50 or `power > 0x46`): random
   rewrite of bits 3..6 of `nFlags32` with `8 * Game_RandU16(3) - 57`.
6. Sprite destruction, when the slot sprite exists and `sprite[22] != 255` and
   class is not 50/15: `d = (int)(power - 1.5 * sprite[22])` (the constant pair
   lo = 0, hi = 0x3FF80000 is 1.5). Cell damage counters are the four bytes at MapCell
   +16..+19, one per slot. If `sprite[22] >= cellDamage + d` the counter grows by
   `d` (clamped at 0) and 255 is stored as -1. Otherwise the sprite is destroyed:
   slots 1..3 become NULL when `nZ != 0`, else `Unit_GetExtraSpr @ 0x2e59c`;
   slot 0 checks `sprite[50]`, may set `bUnk16 = d - sprite[22]` when `nZ != 0`,
   calls `Unit_GetExtraSpr`, and NULLs the floor sprite when the new
   `sprite[22] <= bUnk16` and `nZ != 0`.
7. Class 50 with `unitPower > 0x41` and an occupant: `uTuSpent += weapon[+182]`,
   and when `uTuSpent >= uHp` it is replaced by `10 * (Game_RandU16(6) + 7) + uHp`,
   then `Unit_EnqueueIfApSpent` with 0.
8. Class 15 with a floor sprite and `unitPower > 0xA`: sets `nFlags32` and
   `nFlags40` to `... | 3` on this cell and walks the cells directly above
   (`nZ + 1 .. 3`, same x/y) applying the same until it reaches a NULL cell.

Globals it uses: `g_pCurCell` @ 0x113998 (the original re-reads the cell through
this global after every helper call), `pCtxBase` @ 0x1139b8.

## Helper briefs for the splash port (read once, port with it)

`MapCell_HasOccupant @ 0x5487c` is already ported (`pufo_mapcell.c`): occupant
counts only when `pUnit != NULL` and `Soldier.bStatus < 3`.

`Unit_GetExtraSpr @ 0x2e59c` - `(unused, Soldier *pUnit)`. Returns NULL unless
the low byte of `multiOrAnim.anim.uFacingResetH` is non-zero, in which case it
returns `Font_GetSprByIndex(fontSys, fontSys[uExtraSprBase + 29] + that byte)`.
So a unit's "extra sprite" is a font-table lookup keyed by two unit fields.

`Unit_EnqueueIfApSpent @ 0x4a3d4` - `(Soldier *pUnit, u8 nArg)`. When
`uTuSpent >= uHp` and bit 5 of `bFlags` is clear: calls
`TactState_EnqueueDeathFlash @ 0x4a29c` unless the unit is large with non-zero
`uHp`, then if the tact queue global `unk_157198` has `bQueueActive == 0` calls
`TactState_KickQueue @ 0x4f334`.

`Unit_ApplyCellDamage @ 0x2e5d0` - walks the cell's TactItem list
(`MapCell + 52` = `pItemList`, `TactItem.pNext`), and for every item whose
`nUnk16` (a Soldier pointer at `TactItem + 16`) is set and whose `bHitFlag`
(`Soldier + 433`) is 0:

- Picks a readiness byte from the *weapon* record (`v4`, the caller's context
  pointer) using two selector bytes: at `+175` 1 -> `abReady[254]`, 2 ->
  `abReady[252]`; otherwise at `+196` 2 -> `abReady[253]`, 3 -> `abReady[255]`,
  else `abReady[256]`. Then `scale = 1 - readiness / 100`.
- `damage = nArg - Soldier.bUnk248 * scale`, minus `Game_RandU16(damage / 4)`,
  clamped at 0.
- Armour `nArmorPoints` absorbs it (clamped to 0, overflow carried), the rest
  comes off `uHp` and is added to `nUnk436`, or `uHp` goes to 0.
- Stun identical to the splash path: `damage >= auStat[1] >> 1` and not large ->
  `bKneelOrPrimed += (damage - (auStat[1] >> 1)) / 10 + 1`.
- Sets `Soldier + 433 = 1`.
- After each item: if the item has no unit (`nUnk16 == 0`) or that unit's
  `uHp == 0` (`Soldier + 10`), and `nArg > 40`, the item is dropped with
  `MapCell_ListRemove @ 0x54b58` and the walk restarts from the new list head.

The stun/damage/armour block is the same code as the slot-3 path in
`Combat_SplashDamageCells`; the port should share one helper rather than
duplicate it.

## Ported: shot resolution block (pufo_combat)

`Combat_SplashDamageCells @ 0x2e7d8` and `Unit_ApplyCellDamage @ 0x2e5d0` are now
in `src/pufo/game/pufo_combat.c`, with `tests/test_combat.c` (43 checks).

Evidence that settled the shapes:

- The hit window is one dword per (row, column, slot) in `Map.gap_TrajRest`
  (Map+0x4EBC), indexed `4 * (41 * row + col) + slot`. Confirmed from
  `Combat_ResolveShot`: `MOV R0, #0x4E00 / ORR R0, R0, #0xBC` builds 0x4EBC, and
  the grid itself is an array of dword cell pointers at Map+0x15338
  (`MOV R2, #0x15000 / ORR R2, R2, #0x338 / LDR R2, [R3, R2]`), 60 dwords per
  row, 4 layers.
- `Unit_ApplyCellDamage` takes a fourth argument in R3. At the splash call site
  it is `a5`, not the Map: `LDR R3, [SP, #0x1C+a5] / MOV R1, R4 / MOV R0, R8 /
  BL Unit_ApplyCellDamage`. The class checks just before it read
  `LDR R3, [R8, #0x14050] / LDRH R0, [R3, #0xC8]`, so Map+0x14050 is a *pointer*
  to a record whose u16 class is at +200. Both records are modelled as
  `FireWeapon` and passed separately, because the original reads class from the
  Map one in some rules and from `a5` in another.
- `MOV R3, #0x1B0 / ORR R3, R3, #1 / LDRB R3, [R6, R3]` confirms the "already hit
  this shot" flag is Soldier+0x1B1 (`Soldier.gap_433`).

Deviations, all deliberate:

1. `Unit_ApplyCellDamage` returns nothing. The original returns a mixed pointer
   (last unit seen, or the cell after a list removal) and none of its five call
   sites uses it.
2. Readiness selection. The original keeps the previous item's scale when a
   selector byte is non-zero but not one of the known values, and its first
   value is uninitialised stack. The port threads the running scale through the
   walk and starts it at 0. Same result for every selector value the game uses.
3. `g_pCurCell` @ 0x113998 is gone. The original re-reads the current cell from
   that global after every helper call; no helper changes it, so the port keeps a
   local pointer.
4. `Unit_GetExtraSpr` is called by the splash walk with a *tile sprite* record,
   not a unit. Its two reads, +0x26 and +0x32, land on `Mc1Rec.nSlot` and
   `Mc1Rec.bExtraSprFacing`, so the reuse is coherent rather than a type bug. The
   lookup is factored into `Rec_GetExtraSpr(nBaseIdx, nFacingByte)` and both
   callers use it.
5. Three `Mc1Rec` gap bytes are now named: `bStrength` at 0x16 (255 means
   unbreakable), `bSplashResist` at 0x19 (tile power needed to rewrite the cell
   flags in throw mode), `bExtraSprFacing` at 0x32 (non-zero means the sprite has
   an extra variant). `gap_2f[0]` at 0x2F keeps its old name and meaning.
6. The two special weapon classes act only on the ceiling slot. The original's
   `if (slot != 3) goto LABEL_115` sits before the class tests; a first reading
   of the pseudocode hid that.
7. The random damage reduction is computed with a `u16` bound. When the damage is
   negative the original's 32-bit bound differs, but both then clamp the result to
   zero, so the outcome is identical.

Still open in this cluster: `Combat_ResolveShot @ 0x2f12c` (the caller, 2828
bytes, 101 blocks), `Tact_ResolveThrowLanding @ 0x31a18`, `Unit_bTryOpportunityFire
@ 0x47c38`, and the action processors `Tact_ProcessFireAction @ 0x50adc`,
`Tact_ProcessMoveAction @ 0x4f3f0`, `Tact_ProcessFallAction @ 0x52adc`.

## Correction: the weapon record is ObjectRecFull, not a new type

`Combat_ResolveShot @ 0x2f12c` settles it. Before the splash walk it does
`pMap->nFireArg6 = (int)pObj`, so the two weapon reads inside
`Combat_SplashDamageCells` - `pMap->nFireArg6 + 200` and `a5 + 200` - are the
same object. `pObj` is the 208-byte `ObjectRecFull` the data layer already parses
out of main.dat (`pufo_boot.h`), so the first-cut `FireWeapon` struct in
`pufo_combat.h` was a duplicate and is gone.

What the four offsets really are:

| offset | IDA called it | it is |
|--------|---------------|-------|
| +0xAF  | -             | `ObjectRecFull.bFireType`, the readiness selector and the branch selector of the shot resolution (0 direct hit, 1 and 2 area fire) |
| +0xB6  | -             | `ObjectRecFull.uFirePower`, written into the hit window at the impact cell and added to a unit's `uTuSpent` for sort key 50 |
| +0xC4  | `gap_B9[11]`  | newly named `ObjectRecFull.bReadySel`, the readiness selector used when `bFireType` is zero |
| +0xC8  | -             | `ObjectRecFull.uSortKey`, the weapon id and the index into the shot sound table `off_AA040` |

So the "weapon class 50 / 15" tests in the splash walk are `uSortKey`
comparisons, and the TU drain for sort key 50 spends `uFirePower`, not a separate
cost field. The macros are now `PUFO_WEAPON_SORT_50` and `PUFO_WEAPON_SORT_15`.

`ObjectRecFull.gap_B9[11]`, which `Combat_ResolveShot` reads for its own
readiness choice, is the same byte `Unit_ApplyCellDamage` reads as its second
selector. That is the confirmation that the two functions share one record.

## Facing angles: the game's own pi family

`Unit_bFacingAngleDuel @ 0x4dcf4` is ported (`pufo_unit.c`), and it decodes the
angle constants the whole tactical layer shares. They are the game's own
approximations, not the math library's:

| value | appears as (lo, hi) | meaning |
|-------|---------------------|---------|
| 0.785375   | (-1065151889, 1072243146) | one facing step: facing angle = `(bFacing - 2) * 0.785375` |
| 6.283      | (-1065151889, 1075388874) | a full turn, added to normalise a negative angle into [0, 2pi) |
| 3.1415     | (-1065151889, 1074340298) | a half turn, the wrap threshold for an angle error |
| 0.19634375 | (-1065151889, 1070145994) | facing step / 4, the tight tolerance that scores 2 |
| 0.3926875  | (-1065151889, 1071194570) | facing step / 2, the loose tolerance that scores 1 |
| 5.497625   | (1752346657, 1075182993)  | seven facing steps, the far bound in `Unit_bTryOpportunityFire` |
| 16.0       | (0, 1076887552)            | voxel cells per map cell, used by `Unit_UpdateVisibility` |
| 240.0      | (0, 1080950784)            | the ray step limit in `Unit_UpdateVisibility` |

Facing 2 points down +X and facing 6 points down -X. The half turn 3.1415 is a
hair *under* the real pi, so a bearing of exactly pi is above the wrap threshold
and gets wrapped to `6.283 - pi`. That is not a rounding accident to "fix": it
changes which side of a head-on pair scores the tight tolerance, and the port
keeps the original's value.

The duel's shape: each side gets `|trunc(facingAngle - bearingToTheOther)|`,
wrapped into [0, half turn], scored 2 / 1 / 0 by the two tolerances; then each
side rolls `Game_RandU16(100)` and loses the roll outright when `auStat[3]` is
below it; then two `Game_RandU16(20)` luck rolls pick one side to scale by
`((delta / 5) * (roll / 2) + roll) * score`, and the larger final value wins.
A side with `auStat[3] == 0` therefore always loses, which is what the test
pins down.

`Unit_bTryOpportunityFire @ 0x47c38` is now fully read but still unported: it
needs `Unit_bFacingAngleDuel` (done), `Unit_SelectBestFireMode @ 0x478e4`, and
`Unit_UpdateVisibility @ 0x48580`. The last one is the big one - a 120-entry
facing ray fan over the cell grid using `dword_B2278` (step vectors) and
`unk_15718C` (per-voxel wall masks), plus the desert/forest ceiling-id rules.
Those two tables are the next thing to recover.

## Brief: Tact_BuildVisRayTable @ 0x37ae4 (read, not yet ported)

`dword_B2278` @ 0xb2278 is **not** a static table. It is built at runtime by
`Tact_BuildVisRayTable`, which is its only writer. The image holds 4800 bytes of
0xFF there. Layout: 120 entries of 10 dwords (40 bytes):

```
[0] dx, [1] dy, [2..9] one enable dword per facing (8 facings)
```

Construction, in order:

1. Four blocks of 30 entries, at `0xb2278`, `0xb2728`, `0xb2bd8`, `0xb3088`
   (1200 bytes each), laying out the boundary of the direction square [-15, 15]:
   - block 0: `dx = i - 15` for i in 0..29, `dy = -15`
   - block 1: `dx = 15`, `dy = i - 15`
   - block 2: `dx = 15 - i`, `dy = 15`
   - block 3: `dx = -15`, `dy = 15 - i`
2. All 120 gate dwords cleared to 0.
3. Eight passes, one per facing `g = 0..7`, set the gate to 1 on 31 consecutive
   entries starting at entry `15 * g`. The index wraps past entry 119 back to the
   start, so facing 7 covers entries 105..119 and 0..15. Each facing therefore
   gets a 31-ray arc of the 120 boundary directions.
4. For every entry whose gate for the current facing is set, the ray from (0,0)
   to (dx, dy) is rasterised and written into `byte_AD720` @ 0xad720 as 4-byte
   records at `4 * (15 * (31 * facing + rayIndex)) + 4 * |t|`, where `t` is the
   running parameter along the dominant axis. The four bytes are the two
   coordinates, a step-direction code (1 forward, 2 backward), and a
   previous-value change code (0 unchanged, 1 increased, 2 decreased). A ray point
   past the limit writes the terminator byte 96 instead.
   The limit is the double pair (0, 1076756480).

The rasteriser uses the fixed-point helper family `_itos @ 0xa6038`,
`_divs @ 0xa607c`, `_muls @ 0xa603c`, `_stoi @ 0xa6040` - the slope is computed
in fixed point and converted back per step. Those four helpers are the next thing
to characterise before this function can be ported; guessing their scale factor
would produce a subtly wrong visibility fan.

`Unit_UpdateVisibility @ 0x48580` consumes this table, and `unk_15718C @ 0x15718c`
(per-voxel wall masks, indexed `y%16 + 16 * spriteMaskId` as u16 rows) is its
other data dependency. Both are still open.

## Finding: the arithmetic helpers are imports, not game code

`_itod @ 0xa6014`, `_dtoi @ 0xa602c`, `addd/subd/muld/divd`, `ltd/gtd/led/ged`,
and the fixed-point family `_itos @ 0xa6038`, `_divs @ 0xa607c`,
`_muls @ 0xa603c`, `_stoi @ 0xa6040` are all in the `.idata` segment. They are
import thunks (bytes of 0xFF, no function, decompilation fails). Hex-Rays calls
them because the WinCE CRT supplies them; they are not part of the 1103 functions.

Consequence for the port: the double helpers need no work - plain C `double`
arithmetic already matches, which is what every ported module does today. The
fixed-point family does need a reimplementation before `Tact_BuildVisRayTable`
can be ported.

From the way `Tact_BuildVisRayTable` uses them, the shape is a Q15 fixed-point
package:

```
v32 = divs(itos(dy), itos(dx));      // slope  = dy / dx
v34 = muls(v32, itos(t));            // offset = slope * t
v35 = stoi(v34);                     // back to int
```

which is the standard way to get `dy * t / dx` without a divide per step. The
reading that fits the value ranges (dx, dy, t are all at most 15) is:

- `itos(x)` : int to signed 16-bit, plain truncation
- `stoi(x)` : signed 16-bit to int
- `divs(a, b)` : `(a << 15) / b`, a Q15 quotient
- `muls(a, b)` : `(a * b) >> 15`

That is a hypothesis from the call pattern, not a fact from the IDB - the thunks
carry no body. It must be confirmed against the original's output (the built
`byte_AD720` table) before the port relies on it, because a different scale
factor produces a visibility fan that is subtly wrong rather than obviously
wrong.

## Brief: Unit_SelectBestFireMode @ 0x478e4 (read, not yet ported)

Picks the weapon and firing mode for the shot and stores them in the fire
context. For each hand (`Soldier.pHandL`, `Soldier.pHandR`):

- A hand is a candidate when it holds an item, has ammo (`nAmmo > 2`) or the
  weapon is unlimited (`ObjectRecFull.bAmmoCap == 255`), is not two-handed
  (`ObjectRecFull.bTwoHanded == 0`), and has at least one non-zero
  `ObjectRecFull.abArmorTu[0..1]`.
- Mode 1 is chosen from `abArmorTu[1]` and mode 0 from `abArmorTu[0]`, each
  costing `auStat[0] * abArmorTu[k] / 100` TU and accepted only when that fits in
  `uTu`. Mode 0 wins when both fit - it is checked second and overwrites.
- An operative whose weapon's faction record says it may not be fired resets that
  hand's cost to 0xFFFF: `aFacTypeTable[3 + 34 * byte_AA968[uSortKey]] == 0`.
- The right hand wins if it survived; otherwise the left hand. Result:
  `pCtxBase.pSelWeapon` and `pCtxBase.nActionMode`, plus
  `nTrajFront0[1088 + 4*mode] = (int)(weapon byte bFireType[mode+1] * auStat[4] * 0.01)`
  and `nTrajFront0[1040 + 4*mode] = the TU cost`. The 0.01 constant is the double
  pair (1202590843, 1065646817) and it is exact.

Data dependencies, both still open:

- `byte_AA968 @ 0xaa968` is a static map from weapon `uSortKey` to a faction
  record index. The shipped bytes start
  `00 01 06 09 02 07 0a 04 03 08 05 0b 0d 0c 12 17 18 16 15 19 0e 0f 11 14 10`
  and continue with 0x61..0x65 and 0x71..0x75 for the special items.
- `aFacTypeTable @ 0x15ef08` is **runtime-built**: the image is all 0xFF there,
  like `dword_B2278`. Its builder has to be found before this function can be
  ported. Records are 34 bytes; only byte +1 is read here.

## Link found: aFacTypeTable is the in-memory research table

`aFacTypeTable @ 0x15ef08` is the same table the save layer already models as
`ResearchTopic` (144 records of 34 bytes, `PUFO_RESEARCH_TOPIC_COUNT`). Its
cross-references are `Save_WriteResearch @ 0x3f558`, `Save_ReadResearch @ 0x406e8`,
`LoadGame @ 0x44bbc`, `Geoscape_TickHourly @ 0x5d1bc`, `Tact_OpenLeftHandMenu @ 0x17030`,
`Tact_OpenRightHandMenu @ 0x17148`, and `Unit_SelectBestFireMode @ 0x478e4`.

So the firing gate in `Unit_SelectBestFireMode`,

```
aFacTypeTable[3 + 34 * byte_AA968[weapon.uSortKey]] == 0   -> this hand may not fire
```

is a research-progress check: a weapon cannot be fired unless its research topic
says so. `byte_AA968` is the weapon-sort-key to research-topic index map.

One thing to pin down before this is ported: the port's `ResearchTopic` field
order was taken from the **on-disk** write order, and the save header says plainly
that the on-disk order is not the in-memory order. Byte +3 of the in-memory record
is what the firing gate reads, so the in-memory layout has to be recovered from
IDB typing or from the access patterns in `Geoscape_TickHourly` and the hand menus
before the gate can be reproduced. Guessing it would make every weapon either
fireable or not fireable.

## Correction and addition: the research gate offset, and the action cost table

The gate offset written above as `aFacTypeTable[3 + 34 * idx]` is off by one. Both
readers spell it `*((_BYTE *)&aFacTypeTable[3] + 34 * idx + 1)`, and `aFacTypeTable`
is a byte array, so the gate byte is at in-memory research-record offset **4**.

`Tact_OpenLeftHandMenu @ 0x17030` and its mirror `Tact_OpenRightHandMenu @ 0x17148`
confirm the gate and add two facts:

- The gate also requires `uSortKey != 24 || Soldier.bUnk422 != 0`. Sort key 24 is a
  weapon only units carrying that flag may use.
- After the gate it calls `Ui_FillActionCosts @ 0x26104`, then scans the twelve mode
  slots that `Unit_SelectBestFireMode` writes - `nTrajFront0[1088 + 4*k]` (the
  per-mode accuracy term) and `nTrajFront0[1040 + 4*k]` (the TU cost) - for the
  first `k` in 0..11 with a non-zero accuracy term and an affordable cost. If none
  qualifies it pushes text record 205 and stops. Otherwise `nActionMode = 12` and
  `nUiScreen = 3`.

So `pCtxBase.nTrajFront0[1040..1135]` is a twelve-entry per-mode action cost table
shared by the opportunity-fire path and the hand menus, and `Ui_FillActionCosts` is
what fills it for the selected weapon.

The in-memory layout question stays open: the port's `ResearchTopic` field order
comes from the on-disk write order, and offset 4 in memory is not necessarily the
same field. That has to be settled from the IDB typing of `aFacTypeTable` before
the gate is ported.

## Fire modes are three, and the weapon record keeps them as parallel arrays

`Unit_SelectBestFireMode` reads the per-mode accuracy as
`pObj->bFireType[mode + 1]`, i.e. byte `0xAF + mode + 1`, and the per-mode TU cost
as `abArmorTu[mode]` at `0xB3 + mode`. Both are indexed by the same mode, and the
armour array has three entries, so there are **three** fire modes:

| mode | accuracy byte | TU cost byte |
|------|---------------|--------------|
| 0 | 0xB0 | 0xB3 `abArmorTu[0]` |
| 1 | 0xB1 | 0xB4 `abArmorTu[1]` |
| 2 | 0xB2 | 0xB5 `abArmorTu[2]` |

`gap_AF[3]` in `ObjectRecFull` is therefore not padding - it is the accuracy
percentages, and the stored value is `mode + 1` indexed off `bFireType`. The
accuracy written into the action table is `(int)(accuracyPercent * auStat[4] * 0.01)`.

`pCtxBase.nTrajFront0[1040..1135]` is twelve slots, not three: three fire modes
plus the other action modes share one cost table. `Tact_OpenLeftHandMenu` scans
all twelve looking for an affordable one.

## `byte_AA968` extent

Dumped from the IDB. Entries 0..25 are a permutation of 0..25; 31..35 are 97..101;
36..48 alternate 1 with 113/114; 49..51 are 115, 117, 116. Past about entry 52 the
region stops looking like a byte map (values appear at stride 4), so the weapon
sort-key to research-index map is roughly 52 entries and the following bytes belong
to something else. The exact end has to be pinned from the IDB typing before a
table is committed to the port.

## Design issue to fix separately (blocks Unit_SelectBestFireMode)

`pufo_mapcell.h` defines `ObjectRec` as `{ u8 abPad[0xA4]; u8 bListPri; }`, 165
bytes, as a stand-in for the 208-byte `ObjectRecFull` in `pufo_boot.h`. `TactItem.pObj`
is typed `ObjectRec *`, but the combat code reads `ObjectRecFull` fields through it
(`bAmmoCap` at 0xAE, `abArmorTu` at 0xB3, `uSortKey` at 0xC8 - the last one is past
the end of `ObjectRec`).

Casting through would work at runtime because the real objects are 208 bytes, but it
is a type lie and it would silently allow out-of-range reads. The clean fix is to
drop the stand-in and type `TactItem.pObj` as `ObjectRecFull *` everywhere. That
touches `pufo_mapcell.h`, `pufo_unit.h`, `pufo_tactui.h` and their tests, so it is a
separate change rather than something to slip into a combat port.

# Combat_ResolveShot @ 0x2f12c is ported

`src/pufo/game/pufo_combat.c`, `Combat_ResolveShot`, with 43 new checks in
`tests/test_combat.c` (102 checks in that file, 35/35 tests pass, clean `/W4 /WX`).

## What the function actually is

Six arguments: `(pMap, nFireTgtX, nFireTgtY, nFireTgtZ, nFireArg5, pObj)`.
`nFireArg5` is the **slot the shot landed in**, not an index of any kind:

| nFireArg5 | meaning |
|-----------|---------|
| 0 | floor sprite |
| 1 | wall A sprite |
| 2 | wall B sprite |
| 3 | ceiling sprite |
| 4 | a unit |

That explains the two branches the decompiler hides behind `if (nFireArg5 != 4)`:
slot 4 with `bFireType == 0` is a direct hit on a unit and runs **no splash walk at
all**; every other combination goes to the splash path.

The impact record is written at `gap_TrajRest[4 * nFireArg5 + 13440]`. 13440 / 4 =
3360 = `4 * (41 * 20 + 20)`, the centre cell of the window, so the shot writes
`(row 20, col 20, slot nFireArg5)` dword 0... except that `4 * nFireArg5` is a
**dword** offset, so it writes the whole record of that slot, and for slot 3 that
record is dword 3, the unit-power half.

## The ceiling-break sweep is only reachable through a ceiling hit

`Combat_ResolveShot` zeroes the entire 41 x 41 x 4 window at `0x2f258` (6724
iterations x 4 bytes = 26896 bytes) and then writes exactly one record. The sweep
at `0x2f328` reads the **low half of dword 3** (`LDRH R2, [R8, #0x4EC8]`, i.e.
`gap_TrajRest + 16 * index + 12`).

So the sweep can only ever see a non-zero unit power when the shot landed in slot
3, because that write *is* dword 3. A first version of the test seeded dword 3
separately and passed for the wrong reason; the port's test now fires the sweep by
shooting into the ceiling slot, which is the only way the original can.

Sweep rule, from `0x2f628..0x2f6f8`:

```
unitPower != 0
&& cell->pCeilSpr != NULL
&& sprite[0x18] != 0                 /* newly named Mc1Rec.bCeilBreakable */
&& unitPower + cell->bUnk19 > sprite[0x16]   /* bStrength */
```
then: promote the walk mode to 2 and `Sound_Play(2)` once, move the impact point to
this cell, call the field-of-view refresh, and mark the record by writing 1 into
the **high half** of dword 3 (`STRH R2, [R8, #0x4ECA]`). The last cell that gave
way wins.

`0x2f698` computes `(double)sprite[0x18] * 2.5` (`muld` against the pair
(0, 0x40040000)) and stores the integer result into the caller slot the
decompiler names `nRange`. It is not dead: it is the range budget of the
field-of-view pass that follows.

## Direct unit damage, 0x2f408..0x2f574

```
readiness = bReadySel == 0 ? unit->abReady252[4]
          : bReadySel == 2 ? unit->abReady252[1]
          : bReadySel == 3 ? unit->abReady252[3]
          : <uninitialised stack slot>          /* the original's else arm */

power  = weapon->uFirePower - (1 - readiness / 100) * unit->bUnk248
damage = power - (u16)Game_RandU16(power / 2)      /* clamped at 0 */
armour: nArmorPoints -= damage; overflow carries into damage, armour floors at 0
health: damage > uHp ? uHp = 0 : (uHp -= damage, nUnk436 += damage)
stun:   damage >= (auStat[1] >> 1) && !Unit_bIsLarge ->
            bKneelOrPrimed += (damage - (auStat[1] >> 1)) / 10 + 1
Unit_EnqueueIfApSpent(pUnit, 0)
```

`(1 - readiness / 100)` is integer division: a target at 100 or more readiness
loses no armour to the hit, and anything below 100 loses all of `bUnk248`. There is
no scale in between.

`0x2f4d8` is `if (nArmorPoints > 150) nullsub_4();` - a call to an empty function.
Noted, not reproduced.

## Vertical splash propagation, 0x2f760..0x2fa4c

Only when the walk mode is non-zero. First the window is copied to the second
window at `gap_TrajRest + 26896` (`memmove`, 0x6910 bytes), then every layer above
and below the impact layer is attenuated and splashed:

```
value = (u16)record - (mode == 2 ? 20 : 4)
if (value > 0 && cell[processedLayer]->pFloorSpr) value -= pFloorSpr->bStrength
value = max(value, 0)
```

Two quirks kept as the original has them:
- cell validity is tested against the **shot's** layer, not the layer being
  processed (`Map_IsValidCell(pMap, x, y, nFireTgtZ)` while the sprite lookup uses
  `apCells[v44]`);
- only the low half of each record is read and written, so the sweep's mark in the
  high half survives the propagation.

## Item drop pass, 0x2fa9c..0x2fc14

Runs after the splash, over layers 1..3 of the whole grid (`10 * nMapDimX` by
`10 * nMapDimY`). A cell with items and no floor sprite drops every item to the
**highest layer below that has a floor sprite** - the loop breaks on
`**(v70 + 21710) != 0`, and `MapCell + 0` is `pFloorSpr`. Items are moved one at a
time with `MapCell_ListRemove` then `MapCell_ListInsertByPri`, which keeps the
list ordered by `ObjectRecFull.bListPri`.

## `Map_ComputeFov @ 0x2deac` argument evidence (still unported)

Hex-Rays renders both call sites as one-argument calls, but the registers say
otherwise:

| site | R0 | R1 | R2 | R3 |
|------|----|----|----|----|
| `0x2f324` area fire | pMap | 0 | 0 | `uFirePower` (not reloaded after the store to the `nRange` slot) |
| `0x2f6e8` ceiling break | pMap | `20 - column` | the row counter | origin X |

The port calls a `ShotHooks.pfnComputeFov` hook at both points and leaves the
argument list to the real function when it is ported.

## `off_AA040 @ 0xaa040` is the shot sound table

360 bytes bounded by the next symbol at `0xaa168`, so 180 u16 entries, indexed by
`ObjectRecFull.uSortKey`. Dumped into `g_aShotSound`. Values used: 1, 2, 4, 9, 10,
18, 21.

## Deviations recorded

1. `off_AA040` is indexed with no bound check in the original; the port clamps
   `uSortKey >= 180` to silence.
2. `bReadySel` values other than 0, 2, 3 make the original read an uninitialised
   stack slot; the port uses 0.
3. The drop pass indexes layer **-1** when no layer below has a floor sprite
   (`SUBS R0, R0, #1 / BPL` lets the counter fall past zero, and the target is
   then `apCells[-1][y][x]`). The port leaves the items where they are.
4. `Map_ComputeFov` and the `unk_157198` writes are hooks, not ports.

# Map_ComputeFov @ 0x2deac: signature settled, port still open

Hex-Rays renders it as `Map_ComputeFov(Map *pMap)`. It is not. The prologue is

```
MOV  R12, SP
PUSH {R0-R3}          ; 0x10
PUSH {R4-R12,LR}      ; 0x28
SUB  SP, SP, #0x10    ; 0x10   -> frame 0x48
```

so stack arguments start at `calleeSP + 0x48`. The callee reads four of them:

| callee read | callee SP offset | caller SP offset | caller writes it with |
|---|---|---|---|
| `LDR R9, [SP,#nLayer]`   | 0x4C | +0x04 | the shot's layer |
| `LDR R8, [SP,#nRange]`   | 0x50 | +0x08 | the range budget |
| `LDR ..., [SP,#nOriginY]`| 0x48 | +0x00 | origin Y |
| `... [SP,#a8]`           | 0x54 | +0x0C | a mode flag, 1 or 2 |

and the caller writes exactly those four slots (`STR R8, [SP,#0x20+nOriginY]` at
`0x2f320`, `STR R9, [SP,#0x20+nLayer]` at `0x2f2e0`, `STR R3, [SP,#0x20+nRange]` at
`0x2f314`, `STR R3, [SP,#0x20+a8]` at `0x2f2fc`). The real signature is

```c
int Map_ComputeFov(Map *pMap, int a2, int a3, int a4,   /* R0..R3 */
                   int nOriginY, int nLayer, int nRange, int a8);
```

The three call sites in `Combat_ResolveShot`:

| site | R1 | R2 | R3 | nOriginY | nLayer | nRange | a8 |
|---|---|---|---|---|---|---|---|
| `0x2f324` bFireType 2 | 0 | 0 | Map+0x14054 | Map+0x14058 | nFireTgtZ | `uFirePower` | 2 |
| `0x2f3a4` bFireType 1 | 0 | 0 | Map+0x14054 | Map+0x14058 | nFireTgtZ | `uFirePower >> 1` | 1 |
| `0x2f6e8` ceiling break | `20 - col` | `20 - row` | origin X | origin Y | nFireTgtZ | `(int)(sprite[0x18] * 2.5)` | unchanged |

So the `nRange` value the ceiling-break path computes with the 6.0 constant is not
dead at all: it is the range budget of the field-of-view pass that follows. The
earlier note in this file that called it a dead store is wrong.

## What the function does

1. Zero a 31 x 31 field of four u16 per entry at `Map + 0x120DC`
   (`MOV R3, #0x120DC`, 31 x 31 iterations x 8 bytes = 7688 bytes). Records are
   indexed by an offset pair in -15..15, which is why the body addresses them as
   `Map + 248 * dy + 8 * dx + 77788`.
2. Walk a static ray table `byte_AD720 @ 0xad720`: four groups of 1860 bytes,
   selected by a loop counter `byte_10DBB4` taking 0, 2, 4, 6. Each group is 31
   rays x 15 steps x 4 bytes. A step record is `dx, dy, wallAflag, wallBflag`, and
   the next step's `dx, dy` are the following two bytes of the same ray.
3. Per step: test the cell, write the remaining range into the field when it is
   larger than what is there, subtract `sqrt(dx*dx + dy*dy) * 8.0` or `* 20.0`
   (`a8 == 1` or the map weapon's `uSortKey == 50` selects 3.0, otherwise 3.5),
   then attenuate on wall A, wall B and ceiling sprites. A wall only attenuates
   when `sprite[0x16] > cell->bUnk17` (or 255) and `sprite[0x2A] == 0` or the cell
   lacks flag bit 2. The cost subtracted is `sprite[0x17]`. The ceiling subtracts
   `sprite[0x17]` too, except at the origin cell.
4. Fold the 31 x 31 field into the 41 x 41 x 4 hit window centred on
   `(a2 + 5, a3 + 5)`, clamping each u16 at 255.

Constants, as the high half of a double with a zero low half:
`1074003968` = 2.5, `1075838976` = 8.0, `1077149696` = 20.0, `1076756480` = 15.0.

`Map_ComputeFov` calls only `Map_IsValidCell` and `sqrt`, so it has no dependency
on the unresolved Q15 helper family. It is portable as soon as the 7440-byte ray
table is dumped. Until then `Combat_ResolveShot` calls it through `ShotHooks`.

# The `*s` helpers are single-precision soft-FPU, not fixed point

This was recorded earlier as an open Q15 hypothesis. It is now resolved, and the
fixed-point reading is wrong.

`_itos @ 0xa6038`, `_muls @ 0xa603c`, `_stoi @ 0xa6040`, `_divs @ 0xa607c` are all
`.idata` entries (import thunks, no body in the image) alongside `_itod @ 0xa6014`,
`_dtoi @ 0xa602c`, `_utod @ 0xa6054`, `_muld @ 0xa6024`, `_addd`, `_gtd @ 0xa6018`.
The name family is the Windows CE coredll soft-FPU export set: the `d` suffix is
double precision, the `s` suffix is single precision. So

```
itos(x)  ->  (float)x
divs(a,b)->  a / b        in float
muls(a,b)->  a * b        in float
stoi(x)  ->  (int)x
```

Independent evidence from the data, not the names: `Tact_BuildVisRayTable` computes
`stoi(muls(divs(itos(v25), itos(v24)), itos(v0)))` with `v24, v25, v0` all in
-15..15, and stores the result into a **byte**. `divs(itos(15), itos(1))` must
therefore survive as 15, and `muls` must then produce 225. No 16-bit fixed-point
format can hold `15 << 15`. Single-precision float is the only reading that fits.

One assumption stays open: whether `_stoi` truncates toward zero or rounds to
nearest. The port assumes truncation (what C does, and what the CE soft-FPU is
documented to do) and this is the one place where a wrong guess would shift a ray
table entry by one.

# `Tact_BuildVisRayTable @ 0x37ae4` brief (next port)

Builds `dword_B2278 @ 0xb2278`, 120 records of 40 bytes (`_DWORD dx, dy; _DWORD
aStep[8]`), then fills `byte_AD720 @ 0xad720`, 4 groups x 31 rows x 15 steps x 4
bytes = 7440 bytes. `byte_AD720` is `.data` initialised to 0xFF - it is runtime
data, which is why dumping it from the IDB gives nothing but 0xFF.

## The 120 rays are the perimeter of the 31 x 31 box

Four loops of 30 records each, at `dword_B2278`, `+1200`, `+2400`, `+3600`
(IDA's `unk_B2728`, `unk_B2BD8`, `unk_B3088` are these bands):

| band | dx | dy |
|------|----|----|
| 0 | -15..14 | -15 |
| 1 | 15 | -15..14 |
| 2 | 15..-14 | 15 |
| 3 | -15 | 15..-14 |

Then all 960 step dwords are cleared.

## The walk

For each of the 120 rays, for each of 8 groups (`v63 = 8` down to 1):

- `v24, v25` = the ray's dx, dy. Pick the dominant axis: `v60 = 0` when
  `|dx| > |dy|` (walk x), else `v60 = 1` (walk y). `v26` is the dominant
  component, and `v55 = v26 / |v26|` is its sign.
- `v0` walks from 0 to `v26` in steps of `v55`.
- Walk-x branch: `other = stoi(muls(divs(itos(dy), itos(dx)), itos(v0)))`, i.e.
  `other = v0 * dy / dx` in float. Store `step[0] = v0`, `step[1] = other`.
- Walk-y branch: `other = stoi(divs(itos(v0), divs(itos(dy), itos(dx))))`, i.e.
  `other = v0 / (dy / dx)`, and `other = 0` when `dx == 0`. Store
  `step[0] = other`, `step[1] = v0`.
- Stop the ray when `sqrt(other*other + v0*v0) > 15.0` (`_gtd` against
  1076756480 << 32 = 15.0, which is why the table has 15 steps): write the
  sentinel **96** into `step[0]` and end the
  ray. That 96 is what `Map_ComputeFov` tests with `if (v17 == 96) break;`.
- Bytes 2 and 3 of each step are the wall-A and wall-B flags: 0 when the
  perpendicular coordinate did not change from the previous step, 1 when it
  increased, 2 when it decreased; and one of the two bytes is set to 1 or 2 from
  `v55` (the walk direction sign).

Row index is `15 * (group * 31 + rayInGroup)`, and the step slot is
`|v0|`, so `byte_AD720[4 * (15 * (31 * group + ray) + |v0|)]`.

## Still to settle before the port is written

1. `_stoi` truncation versus rounding (above).
2. The seeding loop `for (j = 0; j < 4800; j += 600)` with the `__OFSUB__(v18,
   v18 + 30)` guard, which writes 1 into selected `aStep[0]` slots. `v18` advances
   by 15 eight times and the overflow guard drops the last iteration. What that
   seeding means has not been worked out.
3. Why 8 groups produce 4 groups' worth of output: `Map_ComputeFov` reads groups
   selected by `byte_10DBB4` taking 0, 2, 4, 6, while the builder runs 8 passes.
   The mapping between pass index and group index needs one more look.

`Map_ComputeFov` and `Unit_UpdateVisibility @ 0x48580` both wait on this table.

## Correction: `byte_AD720` is eight groups, and group == builder pass

The four-group reading above is wrong. Sizes settle it.

`Map_ComputeFov` indexes the table as `1860 * byte_10DBB4 + 60 * i + 4 * k` with
`byte_10DBB4` in {0, 2, 4, 6}, `i` in 0..30, `k` in 0..14. Maximum byte offset:
`1860 * 6 + 60 * 30 + 4 * 14 = 13016`.

`Tact_BuildVisRayTable` indexes it as `4 * (15 * (31 * pass + ray) + step)` =
`60 * (31 * pass + ray) + 4 * step`, with 8 passes. Maximum:
`60 * (31 * 7 + 30) + 56 = 14876`.

Both fit exactly in **8 groups x 1860 bytes = 14880 bytes**, and the two index
forms are the same shape: `60 * (31 * group + ray) + 4 * step`. So

- the table is 8 groups of 31 rays x 15 steps x 4 bytes;
- `Map_ComputeFov`'s group index **is** the builder's pass index, and it uses only
  passes 0, 2, 4, 6;
- the seeding loop makes exactly 31 rays active per pass, which is why the walk
  loop's `v56` (the active-ray counter) stays in 0..30 and the offsets stay inside
  the table.

`Map_ScanBlastFootprint @ 0x32cd4` also reads `byte_AD720` and is the likely user
of the other four passes.

## Still open before this can be ported

The seeding loop's addressing does not line up with the ray record layout:

```c
*(_DWORD *)((char *)&dword_B2278[2] + v22 + v17) = 1;
/* v17 = 4 * pass, v22 = (600 * pass + 40 * k) mod 4800, k = 0..30 */
```

For pass 0 this lands on dword 2 of rays 0..30, which is `aStep[0]` as expected.
For pass 1 the byte offset is `8 + 600 + 40k + 4`, and 604 is not a multiple of 40,
so it would land mid-record. Either `v17` is not `4 * pass`, or `v22` is not the
simple modulo I read, or the loop writes something other than `aStep[0]`. This has
to be settled from the raw disassembly at `0x37bec..0x37c60`, not from the
pseudocode, before the builder is ported.

## Settled: the seeding loop writes `aStep[pass]`, and the walk tests it

Raw disassembly at `0x37bec..0x37c54`:

```
R4 = 0; R3 = 0; LR = 0
loop:  R1 = R3 + 30;  if (R3 > R1) skip;  R1 = R1 - R3 + 1 = 31
       inner (31 times):
           R2 = (R0 >= 4800) ? R0 - 4800 : R0        ; R0 = 600*pass + 40*k
           R2 += R4                                   ; R4 = 4 * pass
           STR 1, [dword_B2278 + R2 + 8]
           R0 += 40
       LR += 600;  R3 += 15;  R4 += 4
```

The dword index is `(600*pass + 40*k mod 4800)/4 + pass + 2`. Reducing it:

| pass | dword index | lands on |
|------|-------------|----------|
| 0 | 10k + 2 | ray k, `aStep[0]` |
| 1 | 10k + 153 | ray k+15, `aStep[1]` |
| 2 | 10k + 304 | ray k+30, `aStep[2]` |
| p | 10k + 151p + 2 | ray (15p + k) mod 120, `aStep[p]` |

The `mod 4800` wrap is what keeps pass 7 inside the array: for k >= 15 the offset
wraps and the ray index cycles back to 0.

So `aStep[pass]` is the per-pass active flag, and the walk loop's `v57[2] != 0`
test reads it because `v58 = dword_B2278` is advanced by **one dword per pass**
(`++v58` at `0x380ac`), making `v57[2]` dword `2 + pass` of the ray record.

`Tact_BuildVisRayTable @ 0x37ae4` is now fully understood and has no remaining
blocker.

# Tact_BuildVisRayTable @ 0x37ae4 is ported

`src/pufo/game/pufo_vistry.{h,c}`, 42 checks in `tests/test_vistry.c` (36 tests
pass, clean `/W4 /WX`).

What the checks pin down:

- The 120 rays are the perimeter of the 31 x 31 box in four bands of 30, and the
  bands run `-15..14`, `15 / -15..14`, `15..-14 / 15`, `-15 / 15..-14`.
- Pass `p` activates rays `(15 * p + k) mod 120`, `k = 0..30`. Eight passes, 248
  ray-pass pairs.
- A ray walks its dominant axis (ties walk y) from 0 toward the dominant component
  and **breaks before writing the dominant value itself**, so a row holds exactly
  fifteen steps and no row overruns into the next. The earlier note about a
  four-byte overrun was wrong and the header no longer reserves slack.
- The perpendicular axis is `trunc(v * dy / dx)` in walk-x and `trunc(v / (dy / dx))`
  in walk-y, in single precision, truncating toward zero.
- The ray ends when `sqrt(other^2 + v^2) > 15.0`, writing 96 into the step's byte 0.
  A diagonal ray (-15, -15) ends at step 11; a shallow ray (15, -14) at step 12;
  an axis-aligned ray (15, 0) fills all fifteen steps and never writes a sentinel.
- The change flag lands on the **previous** step: byte 3 in walk-x (`[idx - 1]`),
  byte 2 in walk-y (`[idx - 2]`). The current step's own flag byte is written only
  when the perpendicular axis did not change (0) or, in walk-y, always at byte 3
  from the walk direction sign.

## What is left in this chain

`Map_ComputeFov @ 0x2deac` is now unblocked: its input table is built, its only
calls are `Map_IsValidCell` and `sqrt`, and its eight-parameter signature is
settled. It is the next port, and `Unit_UpdateVisibility @ 0x48580` follows it.

One assumption still stands in `pufo_vistry.c`: `_stoi` truncates toward zero. If
it rounds instead, every perpendicular axis in the table can differ by one. The
diagonal and shallow-ray checks above would shift, so they are the tests to re-run
against a dynamic check if this is ever verified on the original.

# Tact_ResolveThrowLanding @ 0x31a18 is ported

Signature: `(Map *pMap, int nX0, int nY0, int nZ0, int nX1, int nY1, int nZ1,
TactItem *pItem)`. Returns 0 when the throw is out of range, otherwise the
"landed on a valid target" flag. Every callee is already ported: `Game_RandU16`,
`Tact_SimulateThrowArc @ 0x3102c`, `Map_TraceVoxelOccupancy @ 0x3242c`, plus
`sqrt/cos/sin/tan` and the coredll double helpers.

## Range budget from strength and weight

```
w = pSelUnit->auStat[9] * pItem->pObj->bWeight
s = pSelUnit->auStat[8] * pSelUnit->uStunOrArmor
budget = (double)itod64(lo = (s/w)/2, hi = s%w)   /* see below */
if (budget < 1.0)  budget = 1.0
if (budget > 22.0) budget = 22.0
```
`auStat[8]` and `auStat[9]` are the throwing stats; `bWeight` is the item weight.

**The budget is a compiler artifact that became behaviour.** The earlier reading
`(s/w/2) + (s%w)` was wrong. The disassembly at `0x31a44..0x31a78` is:

```
MUL  R0, auStat[9], bWeight          ; w        (LDRB [R2,#0xA5])
MUL  R1, auStat[8], uStunOrArmor     ; s        (LDRH [R1,#0xB2], [R1,#8])
BL   _rt_sdiv                        ; R0 = s/w, R1 = s%w
MOVS R3, R0 / ADDMI R3, R3, #1      ; round the quotient toward +inf
MOV  R0, R3, ASR #1                 ; R0 = quotient / 2
BL   _itod                          ; converts the 64-bit pair R1:R0
```

`_itod` converts a 64-bit integer held in `R1:R0`, and `R1` still holds the
remainder left by `_rt_sdiv`. The high half is stale register state, not a chosen
quantity. The consequence is a two-case rule:

- `s % w != 0` -> the value is at least 2^32 -> clamped to **22.0**.
- `s % w == 0` -> the value is `(s/w)/2` -> clamped into `[1.0, 22.0]`.

So a throw only gets a custom range when the stat product divides exactly by the
weight product; otherwise it gets the maximum. The `ADDMI` also means a negative
quotient rounds toward +inf before the halving. Both are reproduced, and
`Throw_RangeBudget` exposes the raw quotient and remainder so the tests can see
which branch fired.

## Short throws get a random target jitter

When `|nX1 - nX0| < 3 && |nY1 - nY0| < 3`, the destination is pushed 2..5 cells
away in a random direction on each axis: `Game_RandU16(2)` picks the sign and
`Game_RandU16(3)` the size.

## Range test before the arc

```
d = sqrt((nY1-nY0)^2 + (nX1-nX0)^2) * 0.0625          /* 1/16 */
if (nZ1 >= nZ0) d *= (double)((nZ1 - nZ0) / 8)
if (d > budget) return 0
```
Note the integer `(nZ1 - nZ0) / 8`: a height difference under 8 counts as zero, so
a low throw upwards gets no vertical allowance at all.

## The search loop

`Tact_SimulateThrowArc(pMap, nX0, nY0, nZ0, v15, v18, nZ1, pItem)` fills the aim
block (`dTravel`, `dAngleYaw`, `dAnglePitch`, `nArcDenomLo`, `nTrajBound`,
`nLosX0/Y0/Z0/Z1`, `bTrajHitStop`). Then, while `bTrajHitStop != 0`:

```
dz = |nLosZ1 - nLosZ0|
if (nTrajBound < dz)  dTravel += (nTrajBound / dz) * 0.01
else                  dTravel += 0.5
adMtX = nLosX0 + dTravel * cos(dAngleYaw)
adMtY = nLosY0 + dTravel * sin(dAngleYaw)
adMtZ = nLosZ0 + dTravel*tan(dAnglePitch)
                 - (dTravel^2 * 9.18) / (nArcDenomLo^2 * cos^2 * 2.0)
```
The `9.18` is the throw gravity already recorded for `Tact_SimulateThrowArc`, and
`nArcDenomLo` is the arc denominator the ballistics module keeps.

Abort the arc when the landing point is invalid:
`adMtX <= 0 || adMtY <= 0 || adMtZ <= 0 || adMtX >= 160*nMapDimX ||
adMtY >= 160*nMapDimX || adMtZ >= 47.0` -> `bTrajHitStop = 0`.
Same `>= 160 * nMapDimX` bound as `Tact_TraceProjectileArc`, and 47 is the height
ceiling.

`bLanded = 1` when `dTravel >= nTrajBound - 8.0`.

Then `Map_TraceVoxelOccupancy(pMap, (int)adMtX, (int)adMtY, (int)adMtZ)`; a zero
result also clears `bTrajHitStop` and ends the loop.

## Tail

`nTravelSaveLo/Hi = dTravel` (the same save slot `Tact_ComputeAimVector` uses), and
the function returns `bLanded` only when `dTravel * 0.0625 <= budget`, else 0.

## Constants used here

| pair | value | meaning |
|------|-------|---------|
| (0, 1072693248) | 1.0 | budget floor |
| (0, 1077280768) | 22.0 | budget ceiling |
| (0, 1068498944) | 0.0625 | 1/16, cell-to-range scale |
| (0, 1071644672) | 0.5 | travel bump when the bound is not the limiter |
| (1202590843, 1065646817) | 0.01 | travel bump when the bound is the limiter |
| (-171798692, 1075993640) | 9.18 | throw gravity |
| (0, 0x40000000) | 2.0 | arc denominator scale |
| (0, 1078427648) | 47.0 | landing height ceiling |
| (0, 1075838976) | 8.0 | subtracted from the bound for the landed test |

## What the port pins down

`Tact_ResolveThrowLanding` is in `src/pufo/game/pufo_ballist.c` with
`Throw_RangeBudget`, `ThrowBudget` and `ThrowLanding`; 26 new checks in
`tests/test_ballist.c` (63 in that file, 36 tests pass, clean `/W4 /WX`).

`ThrowArc` now carries the aim-block fields the landing loop reads back: origin
and target XYZ (`nLosX0..nLosZ1`), `dBound` (`nTrajBound`), and `dYaw`
(`dAngleYaw`). No second copy of the aim block was introduced.

Confirmed by tests:

- A flat 160-voxel throw with the hit flag clear on entry runs the loop exactly
  once: `dTravel = 0.5`, `dMtX = 160.5`, `dMtY = 160.0`, not landed, returns 0.
- With the flag set the loop keeps stepping until the sample reaches
  `160 * nMapDimX`, and then the tail test rejects it: `dTravel * 0.0625` is past
  the budget, so it returns 0 even though `bLanded` is true.
- A short throw (8 voxels) is landed on the first step because the landed test is
  `dTravel >= dBound - 8.0` and `dBound - 8` is negative, so it returns 1.
- `|dz| > dBound` selects the other bump: `16/64 * 0.01 = 0.0025`.
- A 1600-voxel throw with an 80-voxel climb is rejected before any arc is solved
  (`100.0 * 10 > 22.0`), while the same run with a 7-voxel climb is not rejected
  at all, because the height factor is integer division by 8.

## Map aim-block offsets, read out of the IDB struct

`read_struct` on the `Map` type gives the authoritative offsets, and they match
the ones derived from disassembly except for the two flag bytes:

| field | offset | type |
|---|---|---|
| `adMtX / adMtY / adMtZ` | 0x13EE8 / 0x13F38 / 0x13F88 | `double[10]` each |
| `nLosX0 / nLosY0 / nLosZ0` | 0x14004 / 0x14008 / 0x1400C | int |
| `nLosX1 / nLosY1 / nLosZ1` | 0x14010 / 0x14014 / 0x14018 | int |
| `dTravel` | 0x14020 | double |
| `dAngleYaw` | 0x14028 | double |
| `dAnglePitch` | 0x14030 | double |
| `nTravelSaveLo / nTravelSaveHi` | 0x14038 / 0x1403C | the two halves of a double |
| `nFireArg5 / nFireArg6` | 0x1404C / 0x14050 | int |
| `bFireHitResult` | 0x1406C | char |
| `bTrajHitStop` | 0x1406D | char |
| `nArcDenomLo` | 0x14078 | int, written as a double |
| `nTrajBound / nTrajBoundHi` | 0x14080 / 0x14084 | the two halves of a double |
| `nMapDimX` | 0x1511B | unsigned char |
| `pSelUnit` | 0x15334 | `Soldier *` |

Two corrections to earlier notes: `0x1406C` is `bFireHitResult`, not the
"arc left the map" flag, and `bTrajHitStop` is the byte after it at `0x1406D`.
`adMtX/Y/Z` are ten-entry double arrays, not single values; only index 0 is used
by the aim, arc and landing code read so far.

`Tact_SimulateThrowArc` also stores the thrown item pointer at
`*(_DWORD *)&pMap->bFireHitResult[4]` (Map+0x14070) and sets
`pMap->bFireHitResult[1] = 1`. That is the handoff to whatever consumes the
throw result, and it is recorded rather than interpreted.

## Who sets `bTrajHitStop`

`xrefs_to_field` on `Map.bTrajHitStop` finds no writer in the arc or landing
code. The only functions touching it are the input handlers
(`Input_OnLeft/Right/Up/Down/OnPointer/OnPointerUp/OnAction/OnSecondary/OnConfirm`,
`UiScreen_OnCancel`, `Input_OnMenuEscape`), `MapView_DrawUnitFacingArrow`,
`Tact_ProcessFireAction` and `Tact_ProcessFallAction`.

The landing loop is `while (1)` with the break at the end, so it always runs once;
the flag only decides whether it runs again. The port therefore takes it as an
input (`bHitStopIn`) and returns the final value, instead of inventing a default.

# Map_ComputeFov @ 0x2deac is ported

`Map_ComputeFov`, `FovField` and `Fov_EntryIndex` in `src/pufo/game/pufo_vistry.{h,c}`,
31 checks in `tests/test_fov.c` (38 tests pass, clean `/W4 /WX`). It lives beside
`Tact_BuildVisRayTable` because the ray table is its only input.

## What the checks pin down

- The walk runs passes 0, 2, 4 and 6. The original drives this with a global
  counter `byte_10DBB4` starting at 0 and stepping by 2 while the next value stays
  under 8; the port takes the four passes implicitly.
- The field is 31 x 31 entries of four u16, indexed by an offset pair in -15..15.
  In the Map the cleared region is Map+73948 for 7688 bytes and the entries the
  walk touches start at Map+77788, which is 480 entries in: entry index is
  `480 + 31*offY + offX`, and the fold reads it as `31*a + j`, so `a = offY + 15`
  and `j = offX + 15`. Hex-Rays hides this by folding the 73948 into the constant
  `38897`, which is why the writes look like they are 480 entries away from the
  reads. They are not.
- Slot meaning: slot 3 is the running cost, slot 0 is stamped with it whenever this
  visit beats the one already recorded, slot 1 is wall A, slot 2 is wall B.
- The fold lands entry (offX, offY) on hit-window row `a3 + 20 + offY`, column
  `a2 + 20 + offX`, and only the low half of each record dword is touched. The
  upper half survives, and the sum clamps at 255.
- Step cost is `trunc(sqrt(dStepX^2 + dStepY^2) * scale)` where scale is 8.0 when
  `a8 == 1` or the weapon's `uSortKey == 50`, otherwise 20.0. An axial ray spends
  20 per step at the slow scale and 8 at the fast one; the tests follow both to
  the step where the budget would reach zero.
- The wall test is the same pair of conditions `Map_TraceVoxelOccupancy` uses:
  `sprite[22] > cell->bUnk17` (or `== 255`) and `sprite[42] == 0` or the cell's
  `bFlags29` bit 2 clear. A wall with `bBlocksMove` zero never stops charging;
  only a non-zero `bBlocksMove` plus bit 2 opens it.
- The two flag branches are asymmetric and the port keeps the asymmetry. Flag 2
  (the wall of the cell being left) records the slot **before** charging the wall;
  flag 1 (the wall of the cell being entered) charges first and records the
  reduced value. Flag 2 also sets `bFlags57[0] |= 2` on the cell.
- A ceiling charges `sprite[23]` on every cell except the origin cell, and it is
  charged at the **end** of the step, so the entry written at that step does not
  yet include its own ceiling. The test follows this: one step out is 80, two
  steps out is 57, not 77 and 54.

## New field name

`Mc1Rec.bWallRangeCost` at 0x17, split out of `gap_17`. It is `pSpr[23]` in
`Map_ComputeFov`, used for wall A, wall B and ceiling alike, and its partner
`pSpr[22]` is the already-named `bStrength`.

## Still open in this chain

`Combat_ResolveShot` currently carries a `ShotHooks.pfnComputeFov` placeholder.
Wiring the real function into it means passing the `FovField` and the hit window
through the combat context, which is a separate change; the placeholder stays
until that is done deliberately.

`Unit_UpdateVisibility @ 0x48580` is now unblocked: it needs the ray table and
this field, and both exist.

# `Unit_UpdateVisibility @ 0x48580` - read, port-ready brief (not yet ported)

Signature: `int __fastcall Unit_UpdateVisibility(Soldier *pUnit)`. Callees:
`Unit_bIsOperative @ 0x49eb8`, `Unit_bIsLarge @ 0x49ecc`, `Unit_ScanVisibleUnits
@ 0x4834c`, `Unit_SpotCellOccupant @ 0x48138`, `MapCell_SetBFlags29_4 @ 0x548c0`,
`Map_IsValidCell`, `Map_TraceLos @ 0x30890`.

## The eight ray-table passes are the eight facings

`unk_B2278[i + 2 + pUnit->bFacing] != 0` is the ray's `aPass[bFacing]` flag. That
closes the loop on `Tact_BuildVisRayTable`: the eight "passes" it seeds are the
eight unit facing directions, and `Map_ComputeFov` uses only the even ones
(0, 2, 4, 6) because it is called for the four shot directions.

## Ray walk

```
d       = sqrt(dx*dx + dy*dy) * 16.0
stepX   = (16*dx + 8) / d
stepY   = (16*dy + 8) / d
budget  = 240.0
for t = 0 .. 239:
    x = 16*cell.nX + 8 + t*stepX
    y = 16*cell.nY + 8 + t*stepY
    cell = (x/16, y/16)          break if out of bounds at nLayer
    f = cell->nFlags32 & 7
    if (f != 0 && f != 4)  budget = max(0.0, budget - 2.0*f)
    if (t > budget)        break
    ... wall A, wall B, reveal, ceiling ...
```
So `nFlags32 & 7` is a per-cell sight cost, 2 units each, against a budget of 240.

## Layer choice

`nLayer = pCell->nZ`, raised to `nZ + 1` when the cell has a ceiling sprite whose
signed byte 36 is below -10. The loft row index used for every sprite test is
`spr[(12 * nLayer + 8) % 12 + 8]`, which is the same row-table read
`Los_TerrainRow` already models.

## Vertical scans

Before the rays: the unit's own cell is marked with `MapCell_SetBFlags29_4`; a
large unit also marks three neighbour cells and sets their `bFlags44` bit 0. Then
every layer below the unit is visited where the cell has no occupant or the
occupant's byte 46 is 1, and every layer above where the floor sprite is absent
or its byte 46 is 1. Each visit calls `Unit_SpotCellOccupant`.

During the rays, whenever the ray enters a new cell it reveals upward until a cell
with a floor sprite is found (bounded at layer 3) and downward to layer 0 on the
same rule.

## Wall and ceiling tests

Wall A blocks when `spr[43] != 0` and either `x % 16 == 0` (a ray landing exactly
on a cell boundary) or the loft bit is set, and then only if `spr[42] == 0` or the
cell's `bFlags29` bit 2 is clear. Wall B is the same without the boundary case.
`spr[43]` is a byte not used anywhere else in the port yet; it gates whether the
sprite participates in sight at all.

The ceiling blocks when its loft bit is set, and then a mission-type rule on the
u16 at `ObjectRecFull.bTwoHanded`:
- Desert: block unless the value is in `0x1B..0x41`.
- Forest: block unless the value is in `0x1B..0x40`, with `82` also allowed.
- Any other mission type: block.

## Enemy multi-target scan

For each enemy within 1..15 cells (`trunc(hypot)` of the cell difference):

```
angle        = atan2(ey - uy, ex - ux);  if (angle < 0) angle += 6.283
facingAngle  = (bFacing - 2) * 0.785375; if (angle < 0) facingAngle += 6.283
bFacing == 2:  skip unless 0.785375 <= angle <= 5.497625
otherwise:     accept when abs(trunc(angle - facingAngle)) <= 0.785375
```
`0.785375` is the facing step already recorded for `Unit_bFacingAngleDuel`, and
`5.497625` is seven times it. So the vision cone is one facing step wide on each
side. The `trunc` of the angle difference is a quirk worth keeping.

LOS is traced from `(16*nX+8, 16*nY+8, 12*nZ - ceilSpr->nObjParam + 8)` at both
ends (the `nObjParam` term only applies when a ceiling sprite exists), with
`Map.pSelUnit` temporarily set to the viewer and the enemy written into
`anActionPhaseSave0[120]`, both restored afterwards. On success the enemy cell gets
`bFlags29 |= 0x10` and `bFlags44 |= 1` plus `Unit_SpotCellOccupant`.

## Constants

| pair | value | meaning |
|---|---|---|
| (0, 1076887552) | 16.0 | ray length scale |
| (0, 1080950784) | 240.0 | sight budget, and the 240-step walk limit |
| (-1065151889, 1072243146) | 0.785375 | facing step, also the cone tolerance |
| (-1065151889, 1075388874) | 6.283 | full turn |
| (1752346657, 1075182993) | 5.497625 | 7 * facing step |

## What it needs before it can be ported

`Unit_ScanVisibleUnits`, `Unit_SpotCellOccupant`, `MapCell_SetBFlags29_4` and
`Unit_bIsOperative` first. `Map_TraceLos`, `Map_TraceVoxelOccupancy`, the loft row
test and the ray table are already in the port.

## Correction: `_itod` is int -> double, not a 64-bit pair reader

The throw range budget note in this file previously claimed that `_itod` converts
the 64-bit pair `R1:R0`, so the remainder left by `_rt_sdiv` dominated the value
and any non-exact division forced the 22.0 ceiling. **That was wrong.**

Evidence, from the import table of `pufobeta.exe`:

```
_addd _subd _muld _divd      double arithmetic
_led  _ltd  _ged  _gtd       double comparisons
_itod _utod _stod            int / unsigned -> double
_itos _stoi                  int <-> single
_dtos _dtou _dtoui _dtoi     double -> float / unsigned / int
_rt_sdiv _rt_udiv            integer division
```

There is no `_ltod`. The 64-bit -> double helper is absent from this binary, so
`_itod` is the 32-bit `int -> double` helper. Hex-Rays rendering it as
`itod(a1: sum, a2: stale)` is the same `__PAIR64__` artifact that already misled
the throw-arc straddle reading; the second "argument" is just a register Hex-Rays
guesses at.

Second, independent confirmation from `Map_ScanBlastFootprint @ 0x32cd4`, at
`0x32d68..0x32d90`:

```
LDRSB R0, [R1]           ; dx, the step's absolute x offset
LDRSB R1, [R1,#1]        ; dy
MUL   R2, R1, R1         ; dy*dy
MUL   R1, R0, R0         ; dx*dx   <- R1 left stale
ADD   R0, R2, R1         ; R0 = dx*dx + dy*dy
BL    _itod
BL    sqrt
... led(result, 2.0) == 0 -> break the ray
```

If `_itod` read `R1:R0`, the value would be `dx*dx * 2^32 + (dx*dx + dy*dy)`, so
every step with `dx != 0` would break the ray immediately and the function would
examine nothing. As `int -> double` the test is `sqrt(dx*dx + dy*dy) <= 2.0`,
which is a radius-two footprint: exactly what a blast footprint scan should be.

Corrected rule, now in `Throw_RangeBudget`:

```
budget = clamp((double)((q + (q < 0 ? 1 : 0)) >> 1), 1.0, 22.0)
```

`MOVS/ADDMI` rounds a negative quotient toward zero, `ASR #1` then halves it.
A division remainder has no effect at all. Test expectations updated: `3000/350`
gives **4.0**, not 22.0. IDB comment at `0x31a44` rewritten and the IDB saved.

## `Map_ScanBlastFootprint @ 0x32cd4` — read, port-ready

Returns 1 as soon as any ray touches an occupied cell, 0 after all passes.

- `unk_10DBB4` is the pass counter, started at 0 and stepped while the next value
  stays under 8: **all eight passes**, unlike `Map_ComputeFov`, which uses 0/2/4/6.
- Layers: `for (i = 0; i < 240; i += 60)` walks `apCells` at `240*i`, i.e. layer
  `i/60`, four layers. `nLayer` is a separate counter incremented in the same loop
  and is what `Map_IsValidCell` receives.
- Rays: 31 per pass (`v3 = 0..30`), steps `j = 0..14`, record at
  `steps[1860*pass + 60*v3 + 4*j]` — the same 1860-byte pass group and 4-byte step
  record the ray-table port already models.
- Step bytes 0/1 are **absolute** offsets from the origin cell (`a2+26` is cell X,
  `a2+27` is cell Y). Bytes 4/5 are the next step's absolute offsets. Byte 0 of the
  next step equal to 96 is the end-of-ray sentinel.
- Ray length gate: `sqrt(dx*dx + dy*dy) <= 2.0`, so only offsets within radius two
  are examined. `if (j >= 14) continue;` is therefore unreachable in practice.
- Occupancy test is `MapCell_HasOccupant(pCell)` on the cell the step lands on.
  Step 0 of every row is the origin cell itself (`Step(3, 0, 0, 0) == 0` and
  `Step(3, 0, 14, 0) == 14` in the ray table), so the cell the blast is centred on
  is the first cell tested.
- Wall flags are set on the same two cells as `Map_ComputeFov`, with the same
  asymmetry: byte 2 == 2 marks a wall on the cell being left (`pCell->bFlags57[0]
  |= 2`), byte 2 == 1 marks a wall on the cell being entered (`next->bFlags57[0]
  |= 2`). Byte 3 does the same for wall B with bit 1.
- The blocking sprite byte here is **sprite[43]**, not the sprite[42] that
  `Map_ComputeFov` and `Map_TraceVoxelOccupancy` test. A different byte, recorded
  as-is.
- Ceiling gate: `pCeilSpr != 0 && pCeilSpr[0] != 0` breaks the ray.

## `Unit_UpdateVisibility @ 0x48580` ported

Ported into `pufo_unit.c` with `tests/test_vis.c` (26 checks). Four things the
first reading got wrong, all caught by the tests:

**The downward column test reads the cell *above*, not the cell being revealed.**
The occupant index carries a `+3600` dword (one whole layer) that the cell index
does not:

```
v11 = *(dword *)(pCtxBase + 4 * (60*nY + 60*v6 + nX + 25310))   ; occupant, layer L+1
v12 = apCells[240*nY + 240*v6 + 4*nX]                           ; cell,   layer L
```

`25310 - 21710 = 3600` is one 60x60 layer. So a unit standing above hides the cell
below it, unless its own byte `0x2E` is 1. Byte `0x2E` is inside the
`SoldierMultiOrAnim` union, at `SoldierAnim + 0x12`; named `bUnk12`.

**The fan reveals the unit's own cell too.** Step 0 of every ray is the origin
cell, and the column reveal sets `bFlags44` bit 0 there. Only the large-unit 2x2
block is skipped for a small unit.

**Opening a wall needs both halves of the condition.** The gate is
`sprite[0x2a] == 0 || !(cell->bFlags29 & 4)`. With `bBlocksMove == 0` the wall
blocks no matter what the cell flag says; opening it requires a non-zero `0x2a`
*and* bit 2.

**Facing 2's multi-target cone excludes straight ahead.** The accepted bearing
range is `0.785375 .. 5.497625`, which is everything except the +-45 degrees
around 0 that facing 2 points along. The ray fan covers exactly that excluded
wedge, so the fan spots what the cone will not. Isolating the cone in a test needs
the fan disabled, which the test does by setting `nFlags32 & 7 == 7` on every
cell: 14 per visit against a 240.0 budget, so the walk dies after 17 steps, one
cell out.

Mission type ids, from the disassembly at `0x48fa0`: `unk_128AD2` is
`pCtxBase + 0x1511A` = `nMissionType`, **Desert is 6** and **Forest is 2**. The
ceiling id is the u16 at sprite `+0x28`, which is `Mc1Rec.nGlobalIdx` seen through
`*(u16 *)&ObjectRec->bTwoHanded`.

The loft row id a sprite uses is byte `16`, from `(12 * nLayer + 8) % 12 + 8`.
Because `12 * nLayer` is a whole number of layers the expression is always 16; it
is `PUFO_MC1_ROWTAB_TO_Z + 8`, the same row-table read `Map_TraceVoxelOccupancy`
uses at the mid-cell height.

Deviation, recorded: the original indexes `apCells` without bounds checks in
several places (the large-unit neighbours, the fan's current cell, and the column
reveal when `nLayer` is 4). The port ends the walk when its injected lookup
returns NULL.

# `Unit_bTryOpportunityFire @ 0x47c38` — read, port-ready brief (not yet ported)

Signature: `bool __fastcall Unit_bTryOpportunityFire(Soldier *pTarget, Soldier *pShooter)`.
Note the argument order: the *target* comes first. Callees: `Unit_bFacingAngleDuel
@ 0x4dcf4`, `Unit_SelectBestFireMode @ 0x478e4`, `Tact_TraceProjectileArc @ 0x2fc38`,
`Unit_UpdateVisibility @ 0x48580` — all four are already ported.

Gate chain, each step returns false when it fails:

1. **Distance.** `trunc(hypot(dCellX, dCellY))` must be in `1..15`, measured between
   the two cells.
2. **Facing cone.** Identical to the multi-target scan inside
   `Unit_UpdateVisibility`: bearing = `atan2(dy, dx)` normalised into `[0, 6.283)`,
   facing angle = `(bFacing - 2) * 0.785375` normalised the same way. Facing 2
   accepts only bearings in `0.785375..5.497625`; every other facing accepts when
   `|trunc(bearing - facingAngle)| <= 0.785375`. The same three constants, the same
   truncation quirk.
3. **`Unit_bFacingAngleDuel(unk_157180, pShooter, pTarget)`** must return non-zero.
   Its first argument is the `unk_157180` global and is unused by the function.
4. **`Unit_SelectBestFireMode(pShooter)`** must return non-zero.
5. **Arc trace.** `anActionPhaseSave0[120] = pShooter`, then
   `Tact_TraceProjectileArc(map, x0, y0, z0, x1, y1, z1, nFlags = 0)` traced **from
   the target to the shooter**:
   - start: `16*target.nX + 8`, `16*target.nY + 8`, `12*target.nZ + 8`
   - end:   `16*shooter.nX + 8`, `16*shooter.nY + 8`, `12*shooter.nZ + 8`

   There is **no ceiling `nObjParam` term** here, unlike the LOS traces in
   `Unit_UpdateVisibility` and `Unit_ScanVisibleUnits`. Both ends use the plain
   `12 * nZ + 8`.

6. **Confirm the target is actually spotted.** `Unit_UpdateVisibility(pShooter)` is
   run, then the shooter's multi-target list is walked (`Soldier + 0x1C`, stride 4
   on ILP32) looking for `pTarget`.

On success: `Map.bFireStatus0[1] = 0`, `anActionPhaseSave0[120] = 0`, then
`unk_157180->apOppFire[unk_157180->nOppFireCount] = pShooter` and
`nOppFireCount` is incremented. This is the queue `Tact_TryScanOpportunityFire`
consumes.

On arc failure: `anActionPhaseSave0[120] = 0`, `Map.bFireStatus0[1] = 0`, return 0.

**Quirk to keep.** When the arc succeeds but `pTarget` is *not* in the shooter's
multi-target list, the loop exits at `if (v37 >= nMultiTarget) return v10;` with
`v10` still holding `(int)pShooter`. The function returns a non-zero value — true —
and it leaves `anActionPhaseSave0[120]` pointing at the shooter. It also does not
clear `Map.bFireStatus0[1]` and does not enqueue anything. So "target not spotted"
reports success. Recorded as the original's behaviour, not corrected.

Port shape: the `unk_157180` opportunity-fire queue needs a port model
(`apOppFire` / `nOppFireCount`) before this lands; `Map.bFireStatus0[1]` and
`anActionPhaseSave0[120]` already have stand-ins in the port's context structs.

### Ported and tested: `Unit_bTryOpportunityFire @ 0x47c38`

Port: `Unit_bTryOpportunityFire` in `src/pufo/game/pufo_unit.c`. Tests:
`TestTryOpportunityFire` in `tests/test_vis.c`, 11 checks.

The `unk_157180` queue model the brief asked for already existed: `TactCtx272`
(`pufo_turn.h`) carries `apOppFire[60]` and `nOppFireCount`, and `Turn_PickOppFireUnit`
/ `Turn_FireAtOppFireTarget` consume them. No new type was needed. `VisCtx` gained
`pAim`, `pTactCtx` and `pbFireStatus1` instead of a parallel context struct.

Two findings that changed how the function must be read:

1. **The facing cone excludes straight ahead.** For facing 2 the accepted bearings
   are `0.785375 .. 5.497625` — everything except the ±45° wedge around +X. A target
   directly along the facing axis is therefore *never* offered opportunity fire. The
   same wedge is exactly what the ray fan covers, so the cone and the fan overlap only
   on the diagonals. A test target has to sit on a diagonal (bearing pi/4) to be both
   inside the cone and reachable by the fan.

2. **The angle duel is decided by a raw roll, not by stats.** `Unit_bFacingAngleDuel`
   scales only the side with the higher luck roll; the other side keeps its raw
   `Game_RandU16(100)` value, and the comparison is `nRollA > nRollB`. `auStat[3]`
   only zeroes a roll that comes out above it — it never creates a non-zero one. So a
   shooter whose reaction roll truncates to 0 loses the duel whatever its stats and
   facings are. `Game_RandU16` is `(u16)((float)(rand() % 32768) / 32768.0f * nMax)`,
   and an unseeded generator's first value (41) truncates to 0 at nMax = 100. The test
   seeds with 89, whose first draw is 329 and gives a roll of 1.

The quirk is reproduced and asserted: target passes every gate, is not on the shooter's
multi-target list, and the function still reports success, leaves the scratch slot
pointing at the shooter, leaves `Map.bFireStatus0[1]` untouched, and enqueues nothing.

Build note: `pufo_combat.c` calls `Sound_Play`, and `pufo_unit.c` now references
`Unit_SelectBestFireMode`, so `pufo_game` links `pufo_audio` publicly rather than every
test target repeating it.

### Ported and tested: `Unit_DropAllItemsToCell @ 0x4b4f8`

Port: `Unit_DropAllItemsToCell` in `src/pufo/game/pufo_unit.c`. Tests:
`TestDropItems` in `tests/test_unit.c`, 10 checks.

The decompiler hides the slot map behind a `MapCell` cast, so the offsets are worth
writing down. The belt walk reads `v7[6].pWallSprA` with `v7` advancing four bytes a
step: `4*n + 6*sizeof(MapCell) + offsetof(pWallSprA)` = `4*n + 360 + 4` = `0x16c + 4*n`,
which is `Soldier.apBelt[n]`. The write side of the same loop spells it as
`apBelt[v4] + v6`, and the two agree.

Full slot map:

| Group | Slots | Dropped | Cleared |
| --- | --- | --- | --- |
| Hands | `pHandL`, `pHandR` | only when the unit is **not** large | same guard |
| Belt | `apBelt[0..8]` | all 9 | all 9 |
| Backpack | `apBackpack[0..7]` | 0,1,2,3,4,7 | same six |
| Pockets | `aPockets[0..3].apItem[0..1]` | all 8 | all 8 |

Two details that are easy to get wrong:

- **Backpack slots 5 and 6 are skipped entirely**, not just skipped for dropping. The
  guard is `i != 1 || (j != 1 && j != 2)` over `i` in 0..1 and `j` in 0..3, so a skipped
  slot keeps its item. A unit dying does not shed those two.
- **The pocket walk is two passes of four.** `v12` advances four bytes per pass, so pass
  one lands on `apItem[0]` of each pocket and pass two on `apItem[1]` of the same four.
  `InvPocketPair` is 12 bytes (two item pointers, two meta bytes, two spare), which is
  what makes the four-byte step land on the second item pointer.

`unk_138EA0` is the shared empty-slot placeholder. An inventory slot may hold it instead
of NULL. It counts as "no item" for dropping — it is never inserted into the cell list —
but the slot holding it is still cleared, so the placeholder does not survive a drop.
Named `g_itemSlotEmpty` in the port and commented at `0x138ea0` in the IDB.

All callees of `Tact_KillUnit @ 0x4e430` except `Tact_CheckBattleEnd` are now ported:
`Unit_ApplyCellDamage`, `Unit_bIsLarge`, `Tact_RemoveUnitFromSideLists`,
`Unit_UpdateAnimPose`, `Unit_ResetBattleState`, `Unit_PlaceOnMap`,
`Combat_ResolveShot`, and `Unit_DropAllItemsToCell`.

### Ported: `UnitList_ForEachRefresh @ 0x22b54`

Port: `UnitList_ForEachRefresh(struct UnitRosters *, VisCtx *)` in
`src/pufo/game/pufo_unit.c`. It is a seven-instruction loop: for each entry of
`apXcomUnits[0..nXcomUnitCount)` call `Unit_UpdateVisibility`. The original threads
the last result through as its own return value; no caller uses it, so the port
returns void. It reuses `UnitRosters` rather than inventing a second roster type.

### Port-ready brief: `Tact_KillUnit @ 0x4e430`

All callees are ported except `Tact_CheckBattleEnd @ 0x34fbc`. The three data tables
it reads are now named in the IDB and are read nowhere else in the binary.

**Guard and counters.** Returns immediately when `bFlags & 0x20` is already set.
Clears `nSpottedCount` and `nMultiTarget`. When `nUnitType > 8 && nUnitType != 16
&& nUnitType != 17 && uHp == 0` it increments a dword counter at
`Map.gap_TrajRest[49940]`.

**Death sound.** Types 0..2 use `g_aDeathSoundByType[type]` (all three are 0, i.e.
no sound). Types >= 3 use the fixed sample 22, as does any unit with `bFemale != 0`.

**Chryssalid transform (`nUnitType == 22`).** A separate path that does not kill:
remove from side lists, `bStatus = 1`, `nUnitType = 14`, copy ints from `0xB1370`
into the unit, then load stats from the 32-byte-per-type template at
`unk_A8F38 + 32 * 14` = `0xA8FD8`. That record reads:

| Field | Value |
| --- | --- |
| `auStat[0]`, `uTu` | 80 |
| `auStat[9]` | 60 |
| `auStat[1]` | 100 + rand(20) - 10 |
| `auStat[2]` | 40 + rand(20) - 10 |
| `auStat[3]` | 110 + rand(20) - 10 |
| `auStat[8]` | 60 + rand(20) - 10 |
| `auStat[4]` | template words 14 then 15, both 0 (the second write wins) |
| `bUnk248` | byte 16 = 15 |
| `abReady252[0..4]` | bytes 18,20,22,24,26 = 5,5,20,5,5 |

Then `Unit_ResetBattleState`, `bUnk422 = 0`, and `Unit_PlaceOnMap` at the cell saved
in `Map.apMultiTargets[0]`.

**Death path.** `bFlags |= 0x20`, `Unit_UpdateAnimPose`, `Tact_RemoveUnitFromSideLists`.

**Morale shock**, and it is inverted by unit type:

| Dead unit | Enemy roster | Xcom roster |
| --- | --- | --- |
| `nUnitType <= 8` | `morale - 1000/(3*auStat[2])`, clamp to 100 | `morale - 1000/(3*auStat[2])`, clamp to 0 |
| `nUnitType > 8`, not 16 or 17 | `morale - 500/(3*auStat[2])`, clamp to 0 | `morale - 1000/(3*auStat[2]) + 34`, clamp to 100 |
| 16 or 17 | none | none |

`bMorale` is a byte, and the clamps write `LOBYTE(v) = 0` / `LOBYTE(v) = 100`.

**Corpse placement.** `bStatus = 4`, own cell occupant cleared; a large unit also
clears `(x+1)`, `(y+1)` and `(x+1,y+1)`. If the dead unit is `Map.pSelUnit`, the
selection becomes `apXcomUnits[0]`. The unit is then appended to the **civilian**
list (`apCivilians[nCivilianUnitCount++]`), which is where corpses live.

A `TactItem` is allocated (0x18 bytes), `TactItem_ctor`'d, and its `pObj` set to
`apObjects[g_aCorpseObjByType[nUnitType]]`. `nUnk16` is set to the unit pointer only
when `uHp != 0`. The item is inserted into the first cell at or below the unit's
layer that has a floor sprite, walking down with `--nZ` until `nZ < 0`. A large unit
does this four times, one per footprint cell, with object indices
`g_aCorpseObjQuadrant[4*nUnitType + 1]`, `+2`, `+3` for the other three.

**Fall damage.** Confirmed from the ARM at `0x4eefc` rather than from the pseudocode:
`LDRB R3, [R3,#0x1C]` reads the found cell's own `nZ` byte, `RSB R3, R8, R3` subtracts
R8 (the loop's layer index), then `SUB R0, R3, #1`. So `nFall = pCell->nZ - nZFound - 1`
really does mix two different quantities: a stored cell attribute and an array index.
They are equal only when the map stores `nZ` equal to the layer slot, and then `nFall`
is -1, the product clamps to 0, and a kill does no fall damage at all. On tiles where
the cell's stored `nZ` sits above the slot it was found in, the term is positive and
the unit takes damage. Reproduce it literally; do not simplify it to a layer
difference. Then `v52 = trunc_soft(nFall * auStat[1] * 0.74)` — `LDRH R0, [R5,#0xA4]`
confirms `auStat[1]` — where 0.74 is the soft-FPU constant `1053609165` (0x3F3D70A7 as
a single-precision bit pattern). Clamped to 0 when negative. If `uHp > v52`:
`uHp -= v52` and `nUnk436 += v52`; otherwise `uHp` is set to the accumulated `nUnk436`.

**Inventory.** `Unit_DropAllItemsToCell` runs only when the unit is not large and
`nUnitType != 14` — so a chryssalid transform keeps its gear.

**After the kill.** `Tact_CheckBattleEnd(pMap, 0)`. Then, unless `bGameTickHold` or
`unk_138E74` is set: a unit with `bUnk1037` already set skips the message; otherwise
types > 8 get `bUnk1037 = 1` with no message, and smaller units push a status message
built from `szName` plus a text record — `unk_B38D0[3075]` for a living male,
`unk_B38D0[8610]` / `unk_B38D0[8405]` for a dead female / male under type 4,
`unk_B38D0[218735]` for a dead unit of type 4..8, or the fixed string at `unk_B45A0`
when alive and female.

**Type 18 explosion.** When `nUnitType == 18` and `rand(10) != 0`: set
`nFireOrigX/Y` to the cell, `Combat_ResolveShot(map, x, y, z, 0, apObjects[17])`, then
`Unit_ApplyCellDamage` with power 255 on the cell and its three large-unit neighbours.

**Spotted-flag sweep.** When `apUiSlotsA[2] != 0`: clamp a ±8 tile window to
`10*nMapDimX` / `10*nMapDimY` and clear bit 0 of `MapCell.bFlags44` (byte 44) for
every cell in the window across all four layers, then `UnitList_ForEachRefresh`.

**Remaining dependency.** `Tact_CheckBattleEnd @ 0x34fbc` (5748 bytes) needs ten
helpers, none ported: `Tact_TallyBattleStats`, `Tact_TallyUnitResults`,
`Tact_TallyItemRecovery`, `Tact_TallyCraftRecovery`, `Tact_ScanVictoryTiles`,
`Tact_ClearSoldierBattleFlags`, `Tact_MarkAllSoldiersSeen`, `Tact_GetCameraBounds`,
`Tact_InitMission`, `Tact_Shutdown`. That is the mission-results subtree and is a
separate piece of work from the kill path itself.

### Port-ready brief: the small `Tact_CheckBattleEnd` helpers

Sizes matter for ordering. `Tact_CheckBattleEnd @ 0x34fbc` (5748 bytes) calls ten
helpers; six are small enough to take first, and two are mission lifecycle rather than
battle results:

| Helper | Address | Bytes |
| --- | --- | --- |
| `Tact_MarkAllSoldiersSeen` | `0x33bf0` | 144 |
| `Tact_ScanVictoryTiles` | `0x33264` | 184 |
| `Tact_GetCameraBounds` | `0x3331c` | 368 |
| `Tact_TallyCraftRecovery` | `0x344b4` | 556 |
| `Tact_TallyBattleStats` | `0x3348c` | 756 |
| `Tact_TallyUnitResults` | `0x33780` | 1136 |
| `Tact_TallyItemRecovery` | `0x33c80` | 2100 |
| `Tact_ClearSoldierBattleFlags` | `0x346e0` | 2268 |
| `Tact_Shutdown` | `0x3d568` | 3788 |
| `Tact_InitMission` | `0x3c550` | 4120 |

**Already ported.** Three of the ten are done, under names the coverage tool matches:
`Tact_MarkAllSoldiersSeen` as `SideLists_MarkAllSoldiersSeen` (`pufo_roster.c`),
`Tact_ScanVictoryTiles` and `Tact_GetCameraBounds` (`pufo_tactctl.c`). Their behaviour
is recorded below for reference.

**`Tact_MarkAllSoldiersSeen @ 0x33bf0`.** Walks `apSoldiers[0..nSoldierCount)`. For
each unit with a non-zero `pLinked1040` and `uHp != 0`, increments the u16 at
`pLinked1040 + 250` (a strategic-side "seen" counter) and clears `pLinked1040` back to
0. Note it re-reads `nSoldierCount` from the global inside the loop.

**`Tact_ScanVictoryTiles @ 0x33264`.** Only does anything when `nCraftTileA[0] != 0`
and `nMissionType == MissionType_AlienBase`. It walks a 2x2 block of cells anchored at
`nCraftTileA[4]` (x) and `nCraftTileA[8]` (y) in the 2400-per-layer cell table, and
returns 1 as soon as any of the four cells has a zero dword at cell offset 12 — that is
`pCeilSpr == NULL`, so "an alien-base tile is missing its ceiling" counts as victory.
Otherwise 0.

**`Tact_GetCameraBounds @ 0x3331c`.** Five outputs (x0, y0, x1, y1). Three cases:
- `gap_6556[133] == 0`: full map, `(0, 0, 10*nMapDimX, 10*nMapDimY)`.
- Alien base: a 10-tall band at the bottom, `y0 = 10*nMapDimY - 10`, `y1 = 10*nMapDimY`,
  `x0 = 10*(nMapDimX - 3)`, `x1 = x0 + 2`.
- Otherwise: `x0 = x1 = 10 * pad86309a[20]` and `y0 = y1 = 10 * pad86309a[21]` (a
  degenerate point), then widened by the craft type of
  `apCraft[gap_86272[19]]`: type 1 widens by `+20, +10`, type 0 and type 2 widen by
  `+10, +20`, any other type leaves the point degenerate.

None of the remaining seven is ported: `Tact_TallyBattleStats @ 0x3348c`,
`Tact_TallyUnitResults @ 0x33780`, `Tact_TallyItemRecovery @ 0x33c80`,
`Tact_TallyCraftRecovery @ 0x344b4`, `Tact_ClearSoldierBattleFlags @ 0x346e0`,
`Tact_Shutdown @ 0x3d568`, `Tact_InitMission @ 0x3c550`. The last two are mission
lifecycle rather than battle results and belong with the mission subsystem.

### `Tact_TallyBattleStats @ 0x3348c`: analysed, deliberately not ported yet

Shape: ten output counters, all zeroed on entry. Gate is
`gap_6556[133] != 0 || anActionPhaseSave0[92] == 0` -> return. Then a triple walk over
layers 0..3, `y` in `0..10*nMapDimY`, `x` in `0..10*nMapDimX` (the three loop counters
are the globals `unk_113980`, `unk_10DBAC`, `unk_10DBA8`, and the current cell goes to
`g_pCurCell @ 0x113998`), and for each of the four sprite slots it reads
`Mc1Rec[0x1E]` — a tile category byte the port has not named yet, currently inside
`Mc1Rec.gap_1D[3]`.

Category 2 increments counter A and adds `unk_138E6C` to counter B once per such tile,
so B is a count times a per-tile credit. Categories 3, 5, 7, 8, 9, 10 increment
counters D, C, I, G, J, H. Anything else is ignored. At the end, when
`gap_86272[17] > 3` or the mission is an alien base, counter E is set to `unk_138E70`.

The two globals are the reason this is not ported yet. `xrefs_to` shows both are
written only by `Tact_InitMission @ 0x3c550` and round-tripped through
`Save_ReadTGameData @ 0x44248` / `Save_WriteTGameData @ 0x446a8`, so they are saved
mission state, not scratch. The ARM at `0x3c578` shows exactly how they are produced:

```
ADD  R0, R0, #0xA ; STR R0, [R4,R12]   ; unk_138E6C = <value> + 10
MOV  R0, #0x14    ; BL  Game_RandU16   ; unk_138E70 = rand(20), a 0..19 mission value
```

Neither `Tact_InitMission` nor the TGameData save block is ported, and the ten counters
have no names until their consumer gives them meaning. Porting this function now would
mean inventing ten names and two globals with no evidence behind them. The order that
keeps every claim traceable is: `Save_ReadTGameData` / `Save_WriteTGameData`, then
`Tact_InitMission`, then the tallies, then `Tact_CheckBattleEnd`, then `Tact_KillUnit`.

`Mc1Rec[0x1E]` is worth naming independently — it is a tile category with at least the
values 2, 3, 5, 7, 8, 9, 10 in use, and it is the only reader found so far.

### Hex-Rays error found and corrected in the IDB: the chryssalid name copy

`Tact_KillUnit`'s type-22 path shows up in pseudocode as a copy from
`&MEMORY[0xB1140][560]`, which reads as "copy from a static text blob at 0xB1370".
That is wrong. The ARM at `0x4e4fc..0x4e534` is:

```
LDR   R4, =0x1139B8          ; pMap
MOV   R0, R4 ; BL Tact_RemoveUnitFromSideLists   ; returns pMap, so R0 is still pMap
STRB  R3, [R5,#0xA0]         ; pUnit->nUnitType = 14
ADD   R3, R0, #0x230         ; src = pMap + 0x230
ADD   R1, R0, #0x230
SUB   R0, R5, R3             ; R0 = pUnit - src
ADD   R0, R0, #0xBA          ; R0 += offsetof(Soldier, szName)
loop:
LDRSB R2, [R1]               ; signed byte
STRB  R2, [R0,R1]            ; -> pUnit + 0xBA = szName
MOVS  R3, R2 ; ADD R1,#1 ; BNE loop
```

So it is a plain NUL-terminated **byte** copy of a name from `Map + 0x230` into
`Soldier.szName`. `Map + 0x230` (0x113BE8) is in .bss — a runtime-filled context field,
not a static string. Hex-Rays picked the wrong base register.

Also recorded in the IDB: `pCtxBase` itself. The `Map` type in this database has **one
member**, which is why every field access renders as `gap_*` or `MEMORY[0x1139B8]` and
why several of those names are actively misleading. A comment at `0x1139B8` now lists
the offsets proven from ARM rather than inferred from pseudocode, including `+0x230`,
`+0x51 bFireStatus1`, `+0x5A bGameTickHold`, the `+0x4EBC` hit windows and the counter
at `+0x4EBC + 4*49940`. The struct itself still cannot be edited headlessly — IDA 9.4
has no `ida_struct` module and no `put_udt_details` — so the offsets live as listing
comments until the struct is rebuilt in the GUI.

### Ported with one unverified tail: `Tact_KillUnit @ 0x4e430`

Port: `Tact_KillUnit` in `src/pufo/game/pufo_unit.c`, with `KillCtx` / `KillHooks` in
`pufo_unit.h`. Tests: `TestKillUnit` in `tests/test_vis.c`, 30 checks.

Verified by test: the already-dead guard; `bFlags |= 0x20` and `bStatus = 4`; the cell
occupant cleared; one corpse item per kill with `pObj` from `g_aCorpseObjByType` and
`pLinkedSoldier` set only when the unit died with hit points; the corpse lands on the
unit's cell; the dead unit moves onto the civilian roster; `pSelUnit` moves off the
dead unit; the mission kill counter moves only for types above 8 that are not 16 or 17
and reach zero HP; both morale-shock signs (`500/(3*dex)` for enemies and
`1000/(3*dex) + 34` for Xcom when the dead unit is alien-sized, flipped for a
human-sized death); four corpses and four cleared cells for a large unit with quadrant
objects 125/126/127; and the chryssalid transform (type 22 becomes type 14, status 1,
name copied, no death flag, no corpse, not moved to the civilian roster, stats and
`abReady252` from the template).

**The spotted-flag sweep is now verified.** It clears `MapCell.bFlags44` bit 0 across the +-8 tile window on all four layers and stops at the window edge.

Two fixes came out of chasing it. The port was **not** reassigning `pUnit->pCell` to the cell the corpse landed on, which the original does at `0x4eaa8` (`pUnit->pCell = v36`) before it places the item and only for the first corpse; fall damage, the item drop and the sweep all read the unit from that cell. And the test had a dangling compound literal: `pCombat->pGeom = &(MapGeom){ 4, 4 }` inside a setup function, so the geometry died when the function returned. `nDimX` then read as 0, `nMaxX` became 0, the clamp set `nX1` and `nY1` to -1, and the sweep loop never executed. That is why every earlier probe showed the flag untouched with the branch provably armed.

Two things worth knowing about the original that the tests pinned down:

- **The morale shock divides by `3 * auStat[2]` with no guard.** A rostered unit with
  dex 0 faults. The first version of the test crashed exactly this way.
- **The `+34` morale branch has no low clamp.** Only the `> 100` side is clamped, so a
  negative result wraps through `LOBYTE`. The port assigns through `u8` to keep that.

`Tact_CheckBattleEnd @ 0x34fbc` is reached through `KillHooks.pfnCheckBattleEnd`. That
function is mostly debrief UI and belongs to the `ui-input` task, so the seam is
explicit instead of a stub.

### Readiness check: `Tact_ProcessMoveAction @ 0x4f3f0` (875 pseudocode lines)

Not ported yet, but the dependency survey is done and it is nearly clear. Of its
callees, only one is missing:

| Callee | Address | Port status |
| --- | --- | --- |
| `Unit_RollChance` | `0x25e6c` (596 B) | **missing** |
| `Tact_RefreshTeamVisibility` | `0x4f368` (136 B) | ported, `pufo_turn.c` |
| `Map_FacingDelta` | `0x276a0` (80 B) | ported, `pufo_path.c` |
| `Map_bFacingTurnPositive` | `0x27668` (56 B) | ported, `pufo_path.c` |
| `Map_IsValidCell` | `0x2bc9c` (100 B) | ported, `pufo_mapcell.c` |
| `MapCell_HasOccupant`, `MapCell_SetOccupant`, `MapCell_ListRemove` | — | ported |
| `Tact_ComputeAimVector`, `Tact_TraceProjectileArc`, `Tact_SimulateThrowArc`, `Tact_ResolveThrowLanding` | — | ported |
| `Unit_UpdateVisibility`, `Unit_UpdateAnimPose`, `Unit_RecalcInventoryStat`, `Unit_bIsLarge`, `Unit_bArmorTuWarn` | — | ported |
| `StatusMsg_Push`, `Sound_Play` | — | ported |

So the next step is small and well-defined: port `Unit_RollChance @ 0x25e6c`, then
`Tact_ProcessMoveAction` itself. Behaviour has not been analysed yet — the callee list
is the only claim made here.

### Analysed, not yet ported: `Unit_RollChance @ 0x25e6c`

`Unit_RollChance(Soldier *pUnit, int nChance)` is a chance roll that, on failure,
generates a scatter displacement and stores it in three scratch slots on the unit.

Certain from the pseudocode:

- `v4 = Game_RandU16(100) + 1`, so 1..100. Success is `v4 <= nChance`, which returns 0
  and clears two of the three output slots.
- On failure the magnitude is `Game_RandU16(v4 - nChance)`, i.e. the amount by which the
  roll missed, plus 10, scaled by two soft-FPU constants and a third constant.
- Two angles are drawn as `Game_RandU16(628)` reduced to a value in 0..6.28 — a full
  turn in whole-number hundredths. `cos` and `sin` of those angles multiply the magnitude,
  and the three products are converted back to integers with `dtoi`.
- So the three outputs are a displacement triple, and the function returns the third one.

Not yet resolved, and the reason it is not ported: the output slots render as
`pUnit[82].apSpotted[10]`, `[11]`, `[12]` and one input as `pUnit[78].apSpotted[40]`,
`[41]`. Those are Hex-Rays base-plus-index artifacts, not real fields, and at least one
of the "inputs" is likely another constant pair the decompiler split. The real Soldier
offsets need the ARM at `0x25eec..0x2608c` before any struct fields can be named. Guessing
them here would put invented offsets into a shared struct.

### `Unit_RollChance @ 0x25e6c`: offsets resolved from ARM

The `pUnit[78].apSpotted[40]` and `pUnit[82].apSpotted[10..12]` renderings are wrong in
the same way the chryssalid copy was: the base register is **pCtxBase (R4 = 0x1139B8)**,
not the unit.

```
MOV  R3, #0x14000 ; ORR R3, R3, #0x80   ; R3 = 0x14080
LDR  R0, [R1,R3]!                        ; Map + 0x14080
LDR  R1, [R1,#4]                         ; Map + 0x14084
LDR  R2, =0x147AE148 ; LDR R3, =0x3FD147AE   ; the double 0.22
...
MOV  R3, #0x15058 ; STR R0, [R4,R3]      ; Map + 0x15058
```

So the magnitude input is the aim-block arc-bound double at `Map + 0x14080 / 0x14084` —
the same pair `Tact_TraceProjectileArc` uses — and the displacement triple is written to
`Map + 0x15058` and its two neighbours. `Unit_RollChance` is therefore a context-level
scatter generator, not a per-unit one, and it is the function that fills the aim block
after a failed chance roll.

### `Unit_RollChance` constants decoded

The magnitude chain is exact, not approximate:

- The double pair `0x3FD147AE : 0x147AE148` is **0.27**. (An exponent read by eye gave
  0.22; unpacking it gives 0.27.)
- The soft-FPU constant `1008981770` is the single-precision value **0.01** exactly.

So the scatter magnitude is `Map.arcBound * 0.27 * (miss + 10) * 0.01`, where `arcBound`
is the double at `Map + 0x14080 / 0x14084` and `miss` is `Game_RandU16(roll - chance)`.
That is the whole function's arithmetic; the remaining work is only wiring the three
outputs to `Map + 0x15058` and its neighbours.

### `Unit_RollChance @ 0x25e6c`: ported

Ported to `src/pufo/game/pufo_ballist.c` as
`Unit_RollChance(double dBound, i32 nChance, ScatterOffset *pScatter)`. The scatter triple is
`ScatterOffset { nDx, nDy, nDz }` for `Map + 0x15058 / 0x1505C / 0x15060` (`unk_128A10`,
`unk_128A14`, `unk_128A18`).

Argument correction, settled from ARM rather than pseudocode. Hex-Rays types the first argument
as `Soldier *pUnit`. The ARM is unambiguous: both call sites in `Tact_ProcessMoveAction @ 0x4f3f0`
(at `0x501a0` and `0x50890`) pass R5, and R5 there is the `0x1139B8` context base -- the proof is
`LDR R0, [R5, #(unk_113A00 - 0x1139B8)]` at `0x50850`. The body then reads `[R4, #0x14080]` and
writes `[R4, #0x15058]`. The port therefore takes the two values the function actually touches,
the bound double and the scatter triple, and takes no unit pointer at all.

Control flow, straight from the disassembly:

- `nRoll = Game_RandU16(100) + 1`, so `nRoll` is in `[1, 100]`.
- `nRoll <= nChance` takes the success tail at `0x26084`: all three slots are stored as 0 and the
  function returns 0. The `0x15060` store at `0x2608c` is shared by both paths, so the z slot is
  written on success too.
- Otherwise `nMiss = Game_RandU16((u16)(nRoll - nChance))`. The `LSL#16` / `LSR#16` pair masks the
  miss width to 16 bits before it becomes `nMax`, so a wider miss scatters further.

Magnitude, with the soft-FPU precision steps kept literal:

```
_muld   (double) dBound * 0.27        ; 0x3FD147AE : 0x147AE148
_dtos   -> float
_itos   (nMiss + 10) -> float
_muls   float * float
_muls   float * 0.01f                 ; 0x3C23D70A
_stod   -> double
```

Angles. Each is `Game_RandU16(628)` reduced by `_rt_sdiv` with the divisor 100 in R0 and the
dividend in R1, which is the operand order this binary uses (see `Throw_RangeBudget` and
`MapView_bResolveIsoCell @ 0x1b114`). The division is integer and the remainder is dead, so each
angle is a whole radian in `0..6`. Both draws happen before any trig call, so the stream order is
`rand16(100)`, `rand16(miss)`, `rand16(628)`, `rand16(628)`.

Outputs:

```
nDx = dtoi(cos(angA) * mag)   ; Map + 0x15058
nDy = dtoi(sin(angA) * mag)   ; Map + 0x1505C
nDz = dtoi(sin(angB) * mag)   ; Map + 0x15060, and the return value
```

`xrefs_to` the three globals finds only `Tact_ProcessMoveAction` as a reader. At `0x50208..0x50230`
and `0x508ec..0x50954` it adds the triple to `nX1` / `nY1` / `nZ1`, and on the throw path it
doubles dx and dy with `LSL#1` first (`0x508a0`, `0x508c0`).

Checks: 14 new checks in `tests/test_ballist.c`, 77 total, 0 failures.

- Success clears all three slots and returns 0; `nChance = 100` can never be beaten because the
  roll tops out at 100.
- Draw counts are pinned by comparing the RNG stream after the call against a reference stream
  advanced by hand: one draw on the success path, four on the failure path.
- `nChance = 99` fails only on a roll of 100, and there `nMiss` is `Game_RandU16(1)`, which is
  always 0. That pins the magnitude to `27.0f * 10.0f * 0.01f`, so every slot must be a
  whole-radian sample of 2.7. This is what proves the angle is an integer division rather than a
  real 0..6.28 angle, and that a negative product truncates toward zero instead of flooring.
- A 300-seed sweep keeps every component inside the widest magnitude the roll can produce.

Deviations:

- The original calls `cos`/`sin` from coredll; the port uses host `cos`/`sin`. The inputs are whole
  radians, so the only exposure is a differing libm last ulp, which can only change the integer
  result at an exact boundary.
- `dtoi` is assumed to truncate toward zero, the same assumption already recorded for the arc and
  throw ports.
- The scatter triple is a struct out-parameter, not a live view of the context block. The context
  block itself is not modelled yet; `Tact_ProcessMoveAction` will own it.

---

## Carried over from the previous session, NOT verified: `Tact_ProcessMoveAction @ 0x4f3f0`

Status: still not ported. This section preserves analysis produced in an earlier session
and never committed. It is recorded as leads to re-derive, not as established fact.
Nothing in it was re-checked against ARM in that session; the current session verified
only the cross-references listed near the end.

Function size: 5868 bytes, 236 blocks, cyclomatic complexity 173, 875 pseudocode lines.

Claimed `TactState` scratch offsets (unverified):

| Name | Offset |
|---|---|
| `nStepFrameDiv` | `0x04` |
| `pStepFromCell` | `0x08` |
| `nStepZDelta` | `0x0C` |
| `nStepDrawOffX` / `nStepDrawOffY` | `0x1119C` / `0x111A0` |
| `nStepHeightSrc` / `nStepHeightDst` | `0x111B0` / `0x111B4` |
| `nTraceX` / `nTraceY` / `nTraceZ` | `0x111BC` / `0x111C0` / `0x111C4` |
| `nQueueLen` | `0x111D4` |
| `bQueueActive` | `0x111D6` |
| `nPhaseTick` / `nPhaseMax` | `0x111D8` / `0x111DC` |
| `bUnk70114` / `bUnk70115` | `0x111E2` / `0x111E3` |

Claimed structure: a 19-case move FSM. Four ARM ranges were flagged as still needing
verification before any port:

1. `0x4F718..0x4F794` — large-unit 2x2 occupancy probe.
2. `0x4F8B0..0x4F8EC` — `nStepHeightDst` +/-40 comparison.
3. `0x4FB20` with `TactState_EnqueueVertStep @ 0x49ddc` — `entry.nTuCost` apparently
   reused as a `MapCell *`.
4. `0x50A40..0x50AD0` — civilian swap path.

Two data tables are used by the function and their extents are **not determined**:
`0xA9768` and `0xAA168`. Verified in the current session, and only this:
`xrefs_to 0xA9768` -> `0x5008c`, `0x50358`; `xrefs_to 0xAA168` -> `0x50338`, `0x50354`;
all four inside `Tact_ProcessMoveAction`. A 2048-byte read window at `0xA9768` runs past
the end of its table. The word table at `0xAA168` ends before +0x1A0, where dword data
begins. Name lookups over `0xA9700..0xAA400` returned no rows.

Note the neighbour that is already pinned: `off_AA040 @ 0xaa040`, 360 bytes = 180 u16,
is `g_aShotSound` in `pufo_combat.h`. It is a different table.

The blocker for the port is now gone: `Unit_RollChance @ 0x25e6c`, the one missing
callee from the readiness check above, is ported.


---

## 2026-10-11: Tact_ProcessMoveAction @ 0x4f3f0 pre-port verification + three table fixes

### TactState layout, proven from ARM

Prologue of `Tact_ProcessMoveAction` (0x4f3f0) and `TactState_EnqueueTurn` (0x49b84):

| Offset | Field | Evidence |
|---|---|---|
| 0x00 | bFlag0 | `STRB R6, [R4]` at 0x4f40c |
| 0x01 | bFlag1 | `STRB R11, [R4,#1]` at 0x4f418 |
| 0x04 | nStepFrameDiv | 0x4f8ec |
| 0x08 | pStepFromCell | 0x4f648 |
| 0x0C | nStepZDelta | 0x4f8c8 (R10) |
| 0x10 | nQueueCursor | `LDR R3, [R4,#0x10]` at 0x4f3fc |
| 0x18 | aQueue[1250], stride 56 | `ADD R0, R2, R4` then displacements 0x18/0x1C/0x20 |
| 0x11188.. | tail scratch | |
| 0x1118C | nPhaseCounter | |
| 0x111D4 | nQueueLen (u16) | `MOV R2, #0x111D4` at 0x49b8c |
| 0x111D6 | bQueueActive | |
| 0x111D8 | nPhaseTick | `MOV R3, #0x111D8` at 0x4fa20 |
| 0x111DC | nPhaseMax | `0x11000 | 0x1DC` at 0x4fa40 |
| 0x111E2 | bUnk70114 | |
| 0x111E3 | bUnk70115 | |

Total size 0x111E8 = 70120, which is exactly the size IDA's `TactState` type reports.

The compiler folds the entry address as `(pState + 56*idx) + 0x18`, so ARM displacements
for entry fields are the entry offset plus 0x18: nCmd 0x18, pUnit 0x1C, nFacing 0x20,
nX 0x21, nY 0x22, nTurnDir 0x23, nDelta 0x24, nMode 0x28, nZ 0x30, pWeapon 0x34,
nTuCost 0x3C. The port's `TactQueueEntry` offsets (0x00, 0x04, 0x08, 0x09, 0x0a, 0x0b,
0x0c, 0x18, 0x1c, 0x24, 0x28) are confirmed correct.

### Four ARM ranges the previous session flagged, now settled

1. **0x4F718..0x4F794, the large-unit 2x2 probe.** Four `MapCell_HasOccupant` probes on
   cells at dword deltas +0, +1, +60, +61 from the destination cell, i.e. (x, y),
   (x+1, y), (x, y+1), (x+1, y+1). Each occupied cell is then checked with
   `LDR R3, [cell,#0x30]` / `CMP R3, pUnit`, so MapCell.pUnit at +0x30 is the occupant
   and a cell occupied by this same unit does not block it. Failure goes to 0x50818
   (abort) except the last probe, which goes to 0x4F4AC (status message).
2. **0x4F8B0..0x4F8EC, the step height pair.** `LDRSBNE R1, [R0,#0x24]` then
   `RSBNE R1, R1, #0`: the sprite height is the negated SIGNED BYTE at Mc1Rec+0x24.
   Hex-Rays renders the same field as `gap_22[14]`, which is not offset 0x24 - another
   pseudocode lie. Then `CMP R10,#0 / CMP R10,#1 / MOVEQ R3,#0x28 / CMP R10,R3`:
   nStepZDelta 0 leaves the destination height alone, +1 replaces it with 40, and the
   third case compares against a leftover R3 before `SUB R3, R1, #0x28`.
3. **0x4FB20 with 0x4FB10..0x4FB4C, the climb target cell.** RESOLVED. The ARM
   builds `3600*nZ + 60*nY + nX` (`LDRB R1,[cell,#0x1C]`, `MUL` 60, `+ nY` at +0x1B,
   `MUL` 60, `+ nX` at +0x1A) and loads
   `LDR R5, [ctx + 4*(3600*nZ + 60*nY + nX) + 0x11AF8]`. The constant is
   `MOV R2,#0x11800; ORR R2,R2,#0x2F8` = 0x11AF8, which is exactly
   `0x15338 - 14400`: the grid base with one layer subtracted. So the climb with
   `nFacing == -1` reads the cell at `(nX, nY, nZ - 1)`, one layer BELOW the unit.
   `nLayerScratch` (unk_128AF0, ctx+0x15138) is then set to nZ+1, and the queue-entry
   field at +0x3C really is reused as a `MapCell *` in the
   `nFacing != -1 && nFacing != 1` branch at 0x4FBBC.

   The grid base itself is ctx+0x15338, confirmed independently three times: the
   map-wide `bFlags29 &= ~0x20` sweep at 0x4FE34 uses `MOV R2,#0x15000;
   ORR R2,R2,#0x338` = 0x15338, the step destination at 0x4F68C..0x4F698 uses the
   same pair, and the multi-target clear at 0x504FC uses it again. The earlier
   note that the sweep used 0x11AF8 was a misreading of that ORR pair.
4. **0x50A40..0x50A90, the civilian swap.** apCivilians base is ctx+0x15244,
   apEnemyUnits base is ctx+0x15240, nCivilianUnitCount is ctx+0x15120. The scan finds
   pUnit's index, then `apCivilians[idx] = *(ctx + 0x15240 + 4*nCivilianUnitCount)`
   and decrements the count. Because the two arrays are 4 bytes apart, that source slot
   is `apCivilians[nCivilianUnitCount - 1]`. Hex-Rays' `apEnemyUnits[nCivilianUnitCount
   + 29]` is a base-offset artifact, not a real index.

### Table fixes (all three were wrong in the port)

1. **byte_A7B80 unit-type table: 24 rows, not 16.** `Unit_bIsLarge @ 0x49ecc` is
   `byte_A7B80[type*4 + 1] == 1`. The port's `g_aUnitTypeIsLarge` had 1s at types 11
   and 13; the binary has `{1,0,1,0}` there, so those types are NOT large. Types
   16..23 exist and are reachable (the switch in Tact_ProcessMoveAction has cases up to
   0x16). Fixed: `PUFO_UNIT_TYPE_COUNT` is 24 and both columns are dumped from the
   binary. `test_unit` and `test_enemyai` asserted the old values and were corrected.
2. **g_aShotSoundBySortKey @ 0xAA040 is 148 entries, not 180.** 0xAA040 + 148*2 =
   0xAA168, which is where the second sound table starts. The port's 180-entry array
   ran 32 entries into it.
3. **g_aFireSoundBySortKey @ 0xAA168, 148 u16, new.** Indexed by
   ObjectRec.uSortKey with no bound check; used by Tact_ProcessMoveAction case 0xE
   (0x50338) and case 0x12 (0x50330 -> LABEL_160). Values 3..21.

### New table: g_aUnitMuzzleOffset @ 0xA9768

23 unit-type blocks x 2 groups x 8 facings x 3 i16 (dx, dy, dz), 2208 bytes,
0xA9768..0xAA007. Index proven from ARM at 0x50074..0x50094:

    idx = pUnit->bFacing + 8 * (bSel + 2 * pUnit->nUnitType)   // byte offset = 6*idx

`bSel` is `ObjectRec.bListPri` (pObj+0xA4) clamped to 0 or 1 (0x4FFD8..0x4FFE4). The
three components are added to `16*cell->nX + 8`, `16*cell->nY + 8` and
`12*cell->nZ + (-12*ceilSpr->byte0x24/24)` to build the shot origin (0x50098..0x500e0).
Types 0..13 carry data; 14..22 are zero blocks except type 18 group 1, which is
`{0,0,-3}` for all eight facings. Dumped from pufobeta.exe with tools/dump_muzzle.py.

Tools added: `tools/pe_off.py` (PE RVA -> file offset; IDA addresses in this database
are RVA + 0x10000) and `tools/dump_muzzle.py`.

IDB updated: `unk_A9768` -> `g_aUnitMuzzleOffset`, `word_AA168` ->
`g_aFireSoundBySortKey`, `off_AA040` -> `g_aShotSoundBySortKey`, plus listing comments
at 0xA9768, 0xAA168, 0xAA040 and 0xA7B80.


### Shared pieces of Tact_ProcessMoveAction ported ahead of the FSM

Four behaviours the 19-case switch repeats, pulled out so each is testable on its own:

| Port function | Original | Evidence |
|---|---|---|
| `Map_CellCentreX/Y/Z` | the centre built at 0x5003C..0x50070 and again at 0x50778..0x507A8 | `16*nX+8`, `16*nY+8`, `12*nZ + (-12*pCeilSpr->iCeilParam / 24) + 6`; the ceiling term is 0 with no ceiling sprite, and the division truncates toward zero |
| `Map_ClearShotTraceFlags` | 0x4FDDC..0x4FE90, and the same sweep at 0x4FF08, 0x4F4C4, 0x50638 | ARM 0x4FE20..0x4FE64: 4 layers x `10*nMapDimY` rows x `10*nMapDimX` columns, `bFlags29 &= 0xDF` |
| `Tact_MuzzleOffset` | 0x50074..0x50094 with the clamp at 0x4FFD8..0x4FFE4 | `idx = bFacing + 8*(bListPri(clamped 0/1) + 2*nUnitType)` into `g_aUnitMuzzleOffset` |
| `Tact_ChargeShotAmmo` | 0x502CC..0x50328 | shot counter (ctx+0x2C, unk_1139E4) always incremented; ammo byte at TactItem+6 decremented as `(u8)(nAmmo + 0xFF)` unless `bAmmoCap == 255`, `nAmmo == 0`, or (uSortKey is 13 or 35 while the counter is already non-zero, tested before the increment) |

Deviations: `Map_ClearShotTraceFlags` skips a NULL the injected lookup may return (the
original dereferences the slot unconditionally); `Tact_MuzzleOffset` clamps the unit
type, because `byte_A7B80` has 24 rows while `g_aUnitMuzzleOffset` has 23 blocks and the
original does not bound the index.

`test_ballist` now runs 100 checks (was 77).

## Tact_ProcessMoveAction @ 0x4f3f0 -> Tact_ProcessMoveAction (pufo_tactstate.c)

Ported in full: all 15 handled commands, with 6, 9, 12 and 13 falling through the
default. Each command is a small static function so the ARM ranges map one to one.

| Port function | Original | What was checked in the ARM |
|---|---|---|
| `Tact_ProcessMoveAction` | 0x4F3F0..0x4F438 prologue | R4 = pState, R7 = entry->pUnit, R8 = entry->nTuCost, R9 = 56, R10 = entry->nDelta, R5 = entry->nFacing via `LDRSB` (signed), R6 = 1, R11 = 0 for the whole function |
| `MoveTurn` | 0x4FA2C..0x4FA8C | nPhaseMax = Map_FacingDelta, nTurnDir = +/-1 from Map_bFacingTurnPositive, written to entry byte +0x0B |
| `MoveStep` | 0x4F5D0..0x4F940 | destination `(nX+dx, nY+dy, nZ+nDelta)` from 0x4F63C..0x4F698; the 2x2 probe at deltas +0/+1/+60/+61; the eight draw offsets 0x4F7E0..0x4F860; the footstep gate 0x4F918..0x4F944 |
| `MovePlayStepSound` | 0x4F94C..0x4FA18 | 23-case switch on nUnitType; types 15 and 19 silent; sprite byte 0x1D selects 32 over 19 |
| `MoveTurnTrace` | 0x4FC38..0x4FDA0 | loop bound is the entry's signed nTurnDir; bit 0 set, bit 1 set only when the entry facing is 2 |
| `MoveVert` | 0x4FAE4..0x4FBF4 | signed nFacing -1 / +1 branch, climb height from the ceiling byte + 24, `nPhaseMax = (16*h + 31 if negative) >> 5` |
| `MoveRecover` | 0x5098C..0x50AD8 | roster split at nUnitType > 3, civilian copy-with-last |
| `MoveFire` | 0x4FDC0..0x50350 | cancelled-shot multi-target clear 0x5035C..0x50530, two trace sweeps, muzzle origin 0x4FFC0..0x50100, scatter from Unit_RollChance, mode 7 straight fire |
| `MoveThrow` | 0x50630..0x50984 | two landing resolutions with the doubled scatter, then SimulateThrowArc |

Deviations, all recorded in the source comment:

1. Cell access goes through `MapCellLookup` instead of the flat dword array at
   ctx+0x15338, and a NULL from the injected lookup is skipped where the original
   dereferences unconditionally.
2. `Sound_Play` loses the original's R1 mode (2 for the sprite-selected footsteps,
   4 for the fixed samples); the port settled on the one-argument shape earlier.
3. The `nStepZDelta` test at 0x4F8DC compares against a leftover register instead
   of -1; the port implements the intended `nDelta == -1`.
4. `Unit_UpdateVisibility` is skipped when no `VisCtx` is injected, which is how a
   test drives cmd 0xB.
5. The function returns void. Every original exit either returns a register
   leftover or the result of `Tact_RefreshTeamVisibility`, which the port declares
   void.

Two corrections to what the decompiler shows, both taken from the ARM:

- The occupied target cell lowers `nZ1` by the target cell's own ceiling lift
  (`SUB R7, R7, R0` at 0x50154 where R0 is `(-12 * iCeilParam) / 24`). Hex-Rays
  renders the same subtraction with the opposite sign.
- The civilian roster replacement is `apCivilians[idx] = apCivilians[n - 1]` with
  `--n`. Hex-Rays' `apEnemyUnits[n + 29]` is a base-offset artifact of the same
  load, `MOV LR,#0x15000; ORR LR,LR,#0x240` = 0x15240 = apCivilians - 4.

Also confirmed here: an abort writes 13 into the entry and then
`Tact_RefreshTeamVisibility` overwrites it with the end mark 12, so 13 is only
observable on the cmd 0x10 path, which jumps to LABEL_170 and returns without the
refresh.

New test suite `test_moveaction`, 48 checks over cmds 0, 1, 2, 3, 4, 7, 8, 0xA,
0xF, 0x10, 0xB and 0x12. Cmds 0xE and 0x11 run end to end in `test_mapctx`
against the wired `GameMap`; see the section below.

## The live tactical context @ Map (152632 bytes) -> GameMap (pufo_mapctx.c)

`Tact_ProcessMoveAction` and its callees all read one block: the IDB local type
`Map`, reached in code as the context base at `0x1139b8`. `GameMap` is the port's
version of that block, and `Map_BuildMoveActionCtx` points a `MoveActionCtx` at
it, so the move FSM now runs against real state instead of test stubs.

Field offsets are cited per field in `pufo_mapctx.h`. The ones the move FSM
touches:

| GameMap field | Map offset | IDB name |
|---|---|---|
| `nShotCount` | `0x002C` | `nStatusDrawY` |
| `bFlag22` | `0x0022` | `unk_1139BA` |
| `pSelWeapon` | `0x0048` | `unk_113A00` |
| `bFireStatus1` | `0x0051` | `unk_113A09` |
| `nAnimPhase` | `0x00F8` | `unk_113AB0` |
| `apShotCells[9]` | `0x0004..0x0024` | `nMultiTargetCount` + `apMultiTargets[8]` |
| `adMtX/Y/Z[10]` | `0x13EE8 / 0x13F38 / 0x13F88` | `double[10]` each |
| `aim` | `0x13FD8..0x14088` | the aim block |
| `nFireTickProgress` | `0x1405C` | `unk_127A14` |
| `apTraceExclude[2]` | `0x1406C / 0x14070` | the arc-trace exclusions |
| `bTrajHitStop` | `0x1406D` | ditto |
| `scatter` | `0x15058 / 0x1505C / 0x15060` | `unk_128A10/14/18` |
| `nMaxLayerClamp` | `0x15074` | `unk_128A2C` |
| `pThrowTarget` | `0x150F0` | `unk_128AA8` |
| `nMissionType` | `0x1511A` | `enum MissionType` |
| `geom.nMapDimX/Y` | `0x1511B / 0x1511C` | `unk_128AD3 / 128AD4` |
| `nXcomCount` | `0x1511F` | `unk_128AD7`, byte |
| `nCivilianCount` | `0x15120` | `unk_128AD8`, dword |
| `nEnemyCount` | `0x15124` | `unk_128ADC`, byte |
| `nLayerScratch` | `0x15138` | `unk_128AF0`, signed byte |
| `apXcomUnits[30]` | `0x15154` | `Soldier *[30]` |
| `apEnemyUnits[30]` | `0x151CC` | `Soldier *[30]` |
| `apCivilians[60]` | `0x15244` | `Soldier *[60]`, not `[30]` |
| `pSelUnit` | `0x15334` | `Soldier *` |
| `aCells[4][60][60]` | `0x15338` | `MapCell *[4][60][60]` |
| `status` | `0x0054`, `0x1408C`, `0x140B4` | the status message stack |

Two structural deviations, both recorded in the header:

1. **Field order.** The original interleaves tactical, UI and inventory state in
   one block. `GameMap` groups the tactical part and leaves the UI/inventory
   regions out until a consumer needs them. The block is heap state and is never
   serialised, so order carries no parity weight; the offsets above do.
2. **The grid.** The original keeps `apCells` as a pointer table. `GameMap`
   embeds the 14400 cells and `Map_LookupCell` returns their addresses, and it
   applies `Map_IsValidCell` where the original indexes the table with no bounds
   test at all.

### The shot-history array is the multi-target array

The fire command reads the recorded shot cell as `*(ctx + 4 * nShotCount + 4)` at
`0x50008`. The IDB types that same block as `nMultiTargetCount` @ `0x04` followed
by `apMultiTargets[8]` @ `0x08`. So the "shot history" is not a separate array:
it is the multi-target cell list, read one entry past its count. `GameMap` keeps
one `apShotCells[9]` covering `0x04..0x0024` rather than two names for one block.

### The three callbacks, now real

| Callback | Original | Evidence |
|---|---|---|
| `Map_ArmorTuWarnCb` | `Unit_bArmorTuWarn @ 0x4e254` | the hands are `Soldier.pHandL @ 0x104` / `pHandR @ 0x108`, each read through `TactItem.pObj`: `abArmorTu[3] @ 0xB3`, `bTwoHanded @ 0x28`; the option bytes are the radio bank `0xAC81A / 0xAC83A / 0xAC85A` |
| `Map_PushStatusCb` | `StatusMsg_Push @ 0x2dda4` | `bForce` is 0, the `MOV R2,#0` at the throw-failure call site in the command 0x11 path |
| `Map_UpdateVisibilityCb` | `Unit_UpdateVisibility @ 0x48580` | the call at the end of command 0xB discards the result |

`Unit_bArmorTuWarn` pushes two text records of its own. Their record indices come
from the ARM: index 0 is `byte_B5F40` = `aTextRecs + 205 * 48`, index 1 is
`aTextRecs[205 * slot + 10045]` where `10045 = 205 * 49`, so the record follows
the armour slot the option bank selects. The move FSM's own three records are 1
(`0xB399D`), 2 (`0xB3A6A`) and 4 (`0xB3C04`), all `aTextRecs + 205 * n`.

New test suite `test_mapctx`, 83 checks: grid lookup and bounds, every
`MoveActionCtx` field write-through to the `GameMap`, the real status push
including the busy gate, the armour-TU warning across all four option settings,
the real visibility update, and the FSM running commands 1, 3, 0xE, 0x11 and 0x12
against the live context — including the fire command's ammo rule (`bAmmoCap 255`
never spends a round but still counts the shot) and both throw outcomes.

The throw cases pin down one consequence of the landing loop that is easy to
miss: with `Map.bTrajHitStop` clear the loop runs exactly once, so the travel is
one bump and the landing test `travel >= bound - 8` only passes when the bound is
small. A throw at the unit's own cell (bound 0) lands; a throw at a neighbouring
cell (bound 16 voxels) does not, and the command aborts with aTextRecs record 4.
That is the original's shape, not a port bug: the flag's only writers are the
input handlers, `Tact_ProcessFireAction` and `Tact_ProcessFallAction`.

## Tact_KillUnit @ 0x4e430 — the spotted-flag sweep is now proven

The tail was ported earlier without verification. The ARM at `0x4F24C..0x4F2B8`
settles it:

| ARM | Meaning |
|---|---|
| `LDRB R0, [R4,#(Map_bSweepArmed - 0x1139B8)]` … `BEQ loc_4F2C4` | the whole tail is gated on the byte at `Map+0x20`; clear means straight to the epilogue |
| `ADD LR,R1,#8` / `SUB R5,R1,#8` / `MOV R1,#0xA` / `MUL R0,R3,R1` / `CMP LR,R0` / `SUBGE LR,R0,#1` / `CMP R5,#0` / `MOVMI R5,#0` | window `nX-8 .. nX+8`, high end clamped to `10*nMapDimX - 1`, low end clamped to 0; the same pair for `nY` against `nMapDimY` |
| `ADD R6,#0x3C` … `CMP R6,#0xF0` `BLT` | four layers, 60 rows each |
| `MOV R3,#0x15338` / `LDR R3,[R0,R3]` / `LDRB R11,[R3,#0x2C]` / `AND R11,#0xFE` / `STRB R11,[R3,#0x2C]` | the grid base is `apCells` @ `0x15338` again, and the clear is bit 0 of `MapCell+0x2C`, which is `bFlags44` |
| `BL UnitList_ForEachRefresh` | then a full Xcom visibility refresh |

Three gate bytes are now named in the IDB: `Map_bTickHold` @ `0x113A12`,
`Map_bSweepArmed` @ `0x113A20`, `Map_bFrameScratch` @ `0x138E74`. `test_vis`
asserts the window edge (28 cleared, 29 untouched), all four layers, that only
bit 0 of the byte changes, the clamp at the map edge, and that nothing is cleared
when the gate is off.

## 2026-10-11: Input_OnConfirm @ 0x15f3c — the confirm key

Ported in `src/pufo/game/pufo_input.c`, declared in `pufo_input.h`, asserted by
`test_input` (53 -> 94 checks).

Gate, from the ARM at `0x15F3C..0x15FBC`:

- `bGeoscapeUi` (`Map+0x44`) takes the geoscape branch: `Geoscape_CloseUI(&unk_1864F8)`
  and nothing else.
- otherwise all of `bTactFlag` (`Map+0x00`) non-zero, `bFireStatus2` (`Map+0x52`) zero,
  `bTrajHitStop` (`Map+0x1406D`) zero, `bFireStatus1` (`Map+0x51`) zero and
  `unk_157198->bQueueActive` (`TactState+0x111D6`) zero. That is exactly
  `bTactFlag && TactInput_CanScroll()`.
- `nUiScreen` is read with `LDRH` at `Map+0x3A` and written with `STRH`, so it is a u16.
- 16-case jumptable at `0x15FC0`. Cases 8, 9, 13, 14 and anything above 15 jump straight
  to the epilogue and change nothing.

Case bodies, each cited to its ARM block:

| Screen | Behaviour | ARM |
|---|---|---|
| 0 | `byte_1139BA` (`Map+0x02`) selects commit or cancel | `0x16010` |
| 0 armed | clear `*unk_1571E4 + 9`, clear `Map+0x02`, `MapCursor_SetPos(nCursorX, nCursorY)`, then for `pSelWeapon->pObj->uSortKey` 13 or 35 clear `MapCell.bFlags29` bit 6 over `apMultiTargets[0..n-1]`; count cleared either way | `0x16010..0x160A8` |
| 0 unarmed | `Sound_Play(7, mode 4)` and `bDeployPanelOpen = (bDeployPanelOpen == 0)` | `0x160B8..0x160EC` |
| 1 | `Sound_Play(16, mode 4)` then `Inv_CloseAndCommit` | `0x160F4` |
| 2 | focus the view on the unit stored for the camera cell, clear the screen, sample 16 | `0x1610C` |
| 3, 5, 10, 11, 15 | `UiModal_End(pMap)` then the shared screen-clear tail | `0x161E4` |
| 4 | `bLookCommitFlag` (`Map+0x1508C`) gates adopting `pLookCommitUnit` (`Map+0x15088`) as `pSelUnit` (`Map+0x15334`); `nUiScreen = (byte Map+0x80 != 0)`; both scratch fields cleared; sample 16 | `0x16168` |
| 6 | sample 16, clear the screen | `0x161D4` |
| 7 | sample 14, clear the screen | `0x161CC` |
| 12 | `Options_SaveToRegistry()`, clear the screen | `0x161F0` |

Hex-Rays errors corrected from ARM:

- The multi-target flag renders as `MEMORY[0x1139B8].gap_2[1]`. That is `Map+0x02`, not
  `Map+0x01`: the `Map` type's `gap_2` member starts at offset `0x01` and is `char[3]`, so
  `gap_2[1]` is the byte at `0x02`. The ARM agrees exactly at `0x16010` — `LDRB R3,
  [R4,#(unk_1139BA - 0x1139B8)]`, and `0x1139BA - 0x1139B8 = 0x02`. An earlier note here
  read that subtraction as `0x22` and was wrong; `Map+0x22` has **zero** direct references
  anywhere in the image.
  The follow-up question is now closed: `MapCursor_SetPos @ 0x27014` reads the same byte,
  proven at `0x2723C` (`LDRB R3, [R4,#2]` then `CMP R3, #0`), so a large selected unit only
  spreads the cursor footprint when the latch is zero. The byte is named `Map_bActionLatch`
  in the IDB.
- Case 2's unit lookup renders as a nested `aszStatusMsg[...]` subscript. The ARM is a flat
  array read: base `Map + 0x140F0`, index `3600 * (i8)nLayerScratch + 60 * anCamWorldOff[1]
  + anCamWorldOff[0]`, dword stride, at `0x16144`. `nLayerScratch` is loaded with `LDRSB`,
  so it is signed.

OPEN finding: that array base `Map+0x140F0` sits inside the `Map` type's `aszStatusMsg`
member (`0x140B4`, `char[15][100]`), and a full `4*60*60` dword table there would overlap
`Map.apCells @ 0x15338`, which ARM proves independently at three sites. Either the array is
smaller than the index formula suggests, or the `Map` type's `aszStatusMsg` / `gap_TrajRest`
split is wrong. Not resolved; the port injects the lookup instead of guessing a layout.

Port shape and deviations:

- `TactInputState` gained the fields only this handler touches: `nActionLatch` 0x02,
  `nMultiTargetCount` 0x04, `apMultiTargets[8]` 0x08, `pSelWeapon` 0x48, `pSelUnit` 0x15334,
  `bDeployPanelOpen` 0x15110, `bUiSlot8Byte0` 0x80, `bLookCommitFlag` 0x1508C,
  `pLookCommitUnit` 0x15088, `nCursorX` 0x15128, `nCursorY` 0x1512C, `anCamWorldY` 0x4EB0,
  `pbScratchFlag9` (`*unk_1571E4 + 9`).
- New injected hooks: `pfnGeoscapeCloseUi`, `pfnInvCloseAndCommit`, `pfnSetCursorPos`,
  `pfnFocusOnViewCell`, `pfnSaveOptions`. `Geoscape_CloseUI` and `Inv_CloseAndCommit` are
  not ported yet, so they stay hooks; `MapCursor_SetPos`, `UiModal_End` and
  `Options_SaveToRegistry` exist and can be wired by the caller.
- The original passes its first argument straight to `Inv_CloseAndCommit`; the port drops
  the three unused arguments and passes only state plus hooks.
- The port skips a NULL entry in `apMultiTargets` instead of dereferencing it.

## 2026-10-11: Input_OnSecondary @ 0x156e0 — analysed, port not written

Full ARM read (174 instructions, 33 blocks). Recorded here so the port is a
transcription job, not a re-derivation. IDB comments added at `0x156E0`, `0x15794`,
`0x158F8`, `0x15958`.

Gate: identical to `Input_OnConfirm` (`bGeoscapeUi` takes a `nullsub_8` no-op branch;
otherwise `bTactFlag` plus `TactInput_CanScroll`).

Screen routing (`nUiScreen` @ `Map+0x3A`, u16):

| Screen | Behaviour |
|---|---|
| > 6 | clears the screen only for 10, 11, 12, 15; 13, 14 and > 15 do nothing |
| 6 | clears the screen |
| 1, 2, 4 | `Input_OnConfirm(a1, nUiScreen, a3)` — the second argument is the screen id |
| 3, 5 | nothing |
| 0 | the tactical path below |

Screen 0, two mutually exclusive modes selected by `byte_1139BA` (`Map+0x02`):

- `== 3` (multi-target pop): `n = nMultiTargetCount` (`Map+0x04`); if `n <= 0` return;
  clear `MapCell.bFlags29` bit 6 on `apMultiTargets[n-1]` (`Map+0x08 + 4*(n-1)`);
  `--n`; when the new value is 0 also clear `*unk_1571E4 + 9`.
- otherwise: set `unk_AD712 = 1` (the escape-flag scratch the port already models),
  return unless `MapCursor_IsValid(pMap)`, then:

```
pUnit  = Map.pSelUnit                       /* 0x15334 */
pCell  = pUnit->pCell                       /* Soldier+0x10 */
dx,dy  = aDirDeltaXY[3*pUnit->bFacing]      /* signed bytes */
nX1 = pCell->nX + dx;  nY1 = pCell->nY + dy;  nZ = pCell->nZ

if (!Unit_bIsLarge(pUnit) && (pUnit->bFacing % 2) == 0) {
    if (Map_IsValidCell(pMap, nX1, nY1, nZ)
        && (apCells[nZ][nY1][nX1]->bFlags57[0] & 8)) {        /* MapCell+0x39 bit 3 */
        Map_QueueDoorAction(pMap, pUnit->pCell,
                            apCells[(i8)nLayerScratch - 1][nCursorY][nCursorX],
                            pUnit->bFacing);
        nullsub_4("open door");
    }
}
/* tail, always reached */
pTo = apCells[(i8)nLayerScratch - 1][nCursorY][nCursorX];
if (pUnit->pCell->nX != pTo->nX || pUnit->pCell->nY != pTo->nY) {
    Unit_FaceToward(pUnit, pTo->nX, pTo->nY);
    TactState_KickQueue(unk_157198);
}
```

Notes that matter for the port:

- The door check runs only for **even** facings. `bFacing % 2` is computed through
  `__rt_sdiv` with `R0 = 2` (divisor) and `R1 = bFacing` (dividend), the reversed
  convention already recorded for this toolchain. Even facings are the orthogonal ones
  in `g_aStepDir`, so opening a door "in front of" the unit only happens orthogonally.
- The cursor cell is reached as base `0x11AF8` = `Map.apCells @ 0x15338` minus one layer
  (`14400` bytes), index `3600*(i8)nLayerScratch + 60*nCursorY + nCursorX`. So the cell
  is `apCells[nLayerScratch - 1][nCursorY][nCursorX]` — the same one-layer-down reading
  already proven for `Tact_ProcessMoveAction` case 5.
- `nLayerScratch` is loaded with `LDRSB` (signed) at both sites.
- `MapCell.bFlags57[0]` (`MapCell+0x39`) bit 3 is the door flag; the port already names
  that field.
- All callees exist in the port: `MapCursor_IsValid`, `Unit_bIsLarge`, `Map_IsValidCell`,
  `Map_QueueDoorAction`, `Unit_FaceToward`, `TactState_KickQueue`. Only `nullsub_8` and
  `nullsub_4` are no-ops to skip.
- Hex-Rays again renders `byte_1139BA` as `gap_2[1]`; the ARM is `Map+0x02` (the `Map`
  type's `gap_2` starts at `0x01`, so `gap_2[1]` is `0x02`). This is now the third site with
  that same pseudocode rendering, and an earlier note here mis-subtracted it as `Map+0x22`.

## 2026-10-11: Input_OnSecondary @ 0x156e0 — ported

Ported in `src/pufo/game/pufo_input.c`, declared in `pufo_input.h`, asserted by
`test_input` (94 -> 130 checks).

### The `__rt_sdiv` return registers (correction)

The earlier note in this file said the helper's "result" is in `R1`. It is not. Three
sites prove the full shape — arguments `R0 = divisor`, `R1 = dividend`; **return
`R0 = quotient`, `R1 = remainder`**:

| Site | What it proves |
|---|---|
| `0x50054..0x500CC` (`Tact_ProcessMoveAction`) | `sdiv(24, -12*iCeilParam)`; the ceiling lift is consumed from `R0` at `0x500CC` (`ADD R0,R3,R0`), and the no-ceiling-sprite path pre-sets `R0 = 0` at `0x50070`. The quotient is `R0`. |
| `0x1B114..0x1B130` (`MapView_bResolveIsoCell`) | `nTileY / 2` is done by the compiler with `ASR #1` at `0x1B0F0`, so the helper call exists only for the parity test: `MOVS LR, R1` then `ADDNE R3, R5, #1` is `if (nTileY % 2) v6 += 1`. The remainder is `R1`. |
| `0x15824..0x15840` (`Input_OnSecondary`) | `MOV R0,#2; LDRB R1,[R6,#5]` then `MOVS R3, R1` gates the door path on `bFacing % 2`. Same shape. |

IDB comments added at `0x1583C`, `0x1B128` and `0x5006C`. This also confirms the door
gate is the orthogonal facings `0/2/4/6` — the same four `Map_QueueDoorAction @ 0x2bd00`
accepts, which is why the two agree.

### Ported behaviour

| Part | Evidence | Port |
|---|---|---|
| Geoscape branch | `0x156F0`: `bGeoscapeUi != 0` calls `nullsub_8 @ 0x8b948` and returns | returns without a hook; the geoscape secondary click does nothing |
| Tactical gate | `0x15708..0x1575C`, the same five checks as `Input_OnConfirm` | `bTactFlag && TactInput_CanScroll` |
| Screens 1, 2, 4 | `0x15784`: `Input_OnConfirm(a1, nUiScreen, a3)` | `Input_OnConfirm(pState, pHooks)`; the port has no unused arguments |
| Screens 6, 10, 11, 12, 15 | `0x15764` and `0x159C8..0x159E4` | clear `nUiScreen` |
| Screens 3, 5, 7, 8, 9, 13, 14, > 15 | fall to `locret_159E8` | do nothing |
| Screen 0, `byte_1139BA == 3` | `0x15980..0x159BC` | pop `apMultiTargets[nMultiTargetCount-1]`, `bFlags29 &= ~0x40`, decrement, clear `*unk_1571E4+9` when the count reaches 0; `<= 0` returns |
| Escape flag | `0x157B0`: `bUiEscapeFlag = 1` (renamed from `unk_AD712`) | `*pbEscapeFlag = true`, the same scratch `UiScreen_DismissHigh` clears |
| Cursor test | `0x157C0`: `MapCursor_IsValid(pMap)` | `MapCursor_IsValid` over a `TactCursor` built from `nCursorX/nCursorY/nLayerScratch` |
| Facing delta | `0x157D8`: `aDirDeltaXY @ 0xA7AE0`, signed bytes at `3*bFacing` | `g_aStepDir[bFacing].dx/.dy` |
| Door path | `0x15814..0x158E8` | `!Unit_bIsLarge && bFacing %% 2 == 0 && Map_IsValidCell(...)` and `bFlags57[0] & 8` -> `Map_QueueDoorAction(queue, pUnit->pCell, cursorCell, bFacing, ctx)` |
| Tail | `0x158F8..0x1596C` | cursor cell differs in x or y -> `Unit_FaceToward` then `TactState_KickQueue` |

`Map_pSelUnit` (`0x128CEC`, `Map+0x15334`), `Map_nCursorX` (`0x128AE0`) and
`Map_nCursorY` (`0x128AE4`) are now named globals; the function comment at `0x156E0`
records the whole routing.

### `Unit_FaceToward @ 0x49bf0` is not a facing setter

The port had only the pure `Unit_FacingToward` and injected the rest. The original
reads `pUnit->pCell`, returns when `(nX,nY)` is that same cell, and otherwise
**enqueues a command-0 turn** through `TactState_EnqueueTurn @ 0x49b84`; it never
writes `Soldier.bFacing`. Ported as `Unit_FaceToward(TactState*, Soldier*, i32, i32)`
in `pufo_tactstate.c`, next to the enqueue it performs.

### Deviations

| # | Deviation | Reason |
|---|---|---|
| 1 | `pSelUnit`, `pUnit->pCell`, the queue and the lookup are null-checked | the original dereferences all of them unconditionally; the port must not segfault on an uninitialised context |
| 2 | A `NULL` cursor cell (layer 0 has no layer below) skips the face-and-kick tail | the original indexes `apCells[-1]` and reads whatever is there; the injected lookup returns NULL and the port stops instead of following garbage |
| 3 | `Input_OnConfirm` is called without the original's three arguments | they are unused by `Input_OnConfirm` except as the `Inv_CloseAndCommit` argument, which the port passes through its hook |


## The deploy confirm dialog: `Map_BuildDeployConfirmYesNo @ 0x18a68`

Ported as `Map_BuildDeployConfirmYesNo(const DeployConfirmCtx *)` with the counting
half split out as `Tact_CountDeployUnits`, both in `pufo_tactctl.c`. `test_tactctl`
grew a `TestDeployUnits` block covering the buckets, the alien-base strip, the modal
arguments and both messages.

### The rectangle is not new geometry

ARM `0x18A7C..0x18AC8` builds the rectangle from `Map_nCraftMapX @ 0x15139` and
`Map_nCraftMapY @ 0x1513A` (both renamed this pass), `Map_nMissionType @ 0x1511A`, and
`apCraft[Map_nCraftIndex @ 0x15113]->bCraftType @ +0x02`, where `apCraft` is
`Map + 0x10AC` with a 4-byte pointer stride:

```
MOV R0, #0x10AC
ADD R3, R8, R3, LSL#2        ; R8 = Map
LDR R3, [R3, R0]             ; apCraft[nCraftIndex]
LDRB R3, [R3, #2]            ; bCraftType
```

That is the same rule `Tact_GetCameraBounds @ 0x3331c` already implements, and that
function is already ported with a `CameraBoundsCtx`. The only difference is that the
dialog has no whole-map branch, so the port forces `bUseCraft = true` before calling
it. No second rectangle implementation was written.

### The buckets and the hole

`0x18B5C..0x18CA0` runs the same test over the XCom list and then the civilian list.
The rect test is inclusive on all four edges. Classification:

| `nUnitType` | inside | outside |
|---|---|---|
| `< 4` | `nSmallIn` | `nSmallOut` |
| `4 .. 8` | `nBigIn` | `nBigOut` |
| `> 8` | **not counted at all** | **not counted at all** |

The `> 8` hole is real: a type-9 unit is neither inside nor outside as far as this
dialog is concerned, and the test pins it.

Civilians are skipped when `Soldier.uHp @ 0x00A` is zero (`0x18C24`). Their position
comes from `MapCell.nX/nY @ 0x1A/0x1B` and their type from `Soldier.nUnitType @ 0x0A0`,
identical to the XCom pass.

### The messages

`0x18CB8..0x18D6C` builds two strings with `itoa` and `strcat` into the scratch
buffers `unk_E90D9` and `unk_E9273`:

```
message 1 = itoa(nSmallIn) + text 28 + itoa(nBigIn)   + text 29 + [text 30]
message 2 = itoa(nSmallOut) + text 28 + itoa(nBigOut) + text 29 + [unk_E900C]
```

The fragments `unk_B4F3C`, `unk_B5009` and `unk_B50D6` are `aTextRecs @ 0xB38D0` plus
`205 * 28`, `205 * 29` and `205 * 30` -- runtime-loaded text records, not static
strings, so the port reaches them through `pfnTextRecord`.

The tail of message 2 copies `unk_E900C`. `xrefs_to 0xE900C` returns exactly two data
references, both inside this function, and the address lies past the file-backed
`.data` end at `0xAC800`. Nothing ever writes it, so its first byte is zero and the
copy loop moves only the terminator: message 2 has an empty tail. The port models that
by simply not appending anything, rather than inventing text.

### Deviations

| # | Deviation | Reason |
|---|---|---|
| 1 | `snprintf` replaces the `itoa` + `strcat` chain | same result, no unbounded `strcat` into a fixed scratch buffer; the port takes an explicit `nMsgCap` |
| 2 | `Sound_Play`, the modal and the text records are hooks | the dialog is the only caller that reaches them without a `Map` context; hooks keep the counting half testable without the UI |
| 3 | NULL `pCell` skips a unit | the original reads `pUnit->pCell->nX` unconditionally |

## The second structure IDA merges under `UiPanel`: the 3401-byte line list

`UiPanel_InsertLine @ 0x5a294`, `UiPanel_GetLineFlag @ 0x5a334`,
`UiPanel_GetLineColor @ 0x5a33c`, `UiPanel_AppendLineWithSound @ 0x5a394`,
`UiPanel_RemoveLine @ 0x5a3dc` and `UiPanel_SetLineColors @ 0x5a450` are ported as
`UiLineList_Insert`, `UiLineList_GetFlag`, `UiLineList_GetColor`,
`UiLineList_AppendWithSound`, `UiLineList_Remove` and `UiLineList_SetLine` in the new
`ui/pufo_linelist.{h,c}`, asserted by `test_panel` (`TestLineList`, 27 checks).

They are **not** `UiPanel` members. All six index their first argument the same way:

| field | offset | evidence |
|---|---|---|
| `abLineFlags[200]` | `+0x000` | `*(_BYTE *)(base + nLine)` in `0x5a2f8`, `0x5a338`, `0x5a454` |
| record, 16 bytes | `+0x0C8 + 16*nLine` | `ADD R2, R0, R1, LSL#4` then `STR R3, [R2, #0xC8]` at `0x5a45c` |
| colour slots | record `+0x00, +0x04, +0x08, +0x0C` | `v16[50..53]` in `GetLineColor`, `0xC8/0xCC/0xD4` in `SetLineColors` |
| `nLineCount` (u8) | `+0xD48` (3400) | read in all four mutators |

The capacity is not assumed, it falls out of the arithmetic: `200 + 16*200 = 3400`, so
the flag bytes and the records tile the block exactly and the count byte sits
immediately after them.

The 7092-byte `UiPanel` in `pufo_panel.h` remains the real type of the draw path,
proven two independent ways: `gUiRootPanel @ 0x158890 + apLines(0xBE4) + 4*105 ==
0x159618` is the game-clock field and `+4*106 == 0x15961C` is the geoscape flag, and
`UiScrollList_TrackHold @ 0x3ed44` addresses that same 7092 layout field for field.

The caller that shows the overlay is `ResearchScreen_RebuildTopicList @ 0x74270`: it
passes `(int)&gUiRootPanel` to `UiPanel_AppendLineWithSound` and to
`UiPanel_SetLineColors`, and reads the line count as
`LOBYTE(gUiRootPanel.apLines[89]) - 1` -- `apLines[89]` is offset 3400 of the 7092
layout, which is exactly this structure's count byte. The original overlays the two
views on the same bytes; the port keeps two types and does not model the overlay.

### Deviations

| # | Deviation | Reason |
|---|---|---|
| 1 | The insert shift stops at the 200-entry capacity, and the remove shift breaks there | the original writes `flags[count]` and `record[count]`, which runs past the block when the list is full; the port does not invoke out-of-bounds writes |
| 2 | `AppendWithSound` returns void and takes the sample hook | the original returns `Sound_Play`'s value, which its callers ignore |
| 3 | NULL list and out-of-range indices are handled | the original dereferences whatever it is given |

Kept as-is, not fixed: inserting at an index past the count does **not** clamp. It
writes the slot it was given, leaves a gap, and still increments the count. The test
pins that.

## The geoscape viewport and the strateg list containers

Ported in `game/pufo_strateg.{h,c}`, asserted by `test_strateg` (`TestSmapView`,
`TestStrategListAlloc`, 27 checks).

| Original | Port |
|---|---|
| `Smap_ScrollBy @ 0x820d0` | `Smap_ScrollBy` |
| `Smap_GetTileAtScreen @ 0x82244` | `Smap_GetTileAtScreen` |
| `Tilemap_GetAt @ 0x8217c`, `Tilemap_GetAtAlt @ 0x821a0` | `Tilemap_GetAt` (one implementation) |
| `StrategItem_ctor @ 0x82bd4` | `StrategItem_ctor` |
| `StrategItem_AllocAdd @ 0x415cc` | `StrategItem_AllocAdd` |
| `StrategList_ctor @ 0x83890`, `StrategList_Clear @ 0x838ac` | same names over `PtrArray` |

### One 36-byte block, two IDA names

`0x1863E8` carries both `SmapCamera` (36 bytes: nMode, nScrollX, nScrollY, nMapW,
nMapH, nUnk14, nUnk18, nPickX, nPickY) and `SmapView` (28 bytes: nUnk0, nPosX, nPosY,
nStride, nUnk16, nLastDx, nLastDy). The two agree offset by offset, so they are the
same struct seen for two jobs:

```
0x00 nMode   == nUnk0      0x10 nMapH   == nUnk16
0x04 nScrollX == nPosX     0x14 nUnk14  == nLastDx
0x08 nScrollY == nPosY     0x18 nUnk18  == nLastDy
0x0C nMapW   == nStride
```

Two consequences that the names hide:

- **The scroll position is the view position.** `Smap_ScrollBy` writes `nPosX`/`nPosY`
  and `Smap_ZoomOut` rescales `nScrollX`/`nScrollY` at the same offsets.
- **The tile grid stride is the map width.** `Smap_GetTileAtScreen` wraps x with
  `nMapW` and then indexes the tile bytes with `nStride` -- the same dword at `0x0C`.

### Scroll arithmetic

`pos = pos - delta + last_delta` (0x82104, 0x82114): the view tracks absolute pointer
positions, so the delta applied last time is added back before the new one is removed.
With `halfH = nMapH/2 - 160`, and only when the screen id at `0x186BC4` is not 2:

- `nScrollY + halfH < 0` -> `nScrollY = -halfH` (0x82130)
- then `nScrollY + halfH > nStripHeight + nMapH - 320` -> `nScrollY = nStripHeight -
  halfH + nMapH - 320` (0x82154)

The upper clamp tests the value the lower clamp produced, so they are not independent.
`nStripHeight` is the `uHeight` of the PadImage-shaped object read through `0x186BBC`.
The deltas are stored last, after clamping.

### Screen to tile

`tileX = nScrollX + nMapW/2 + x - 120`, `tileY = nScrollY + nMapH/2 + y - 160`: the view
centre is screen (120, 160). A negative coordinate wraps **once** (0x8228C, 0x82294);
an out-of-range positive one does not wrap at all. The map is a cylinder only on the
low side. `nPickX`/`nPickY` record the resolved coordinate.

### Deviations

| # | Deviation | Reason |
|---|---|---|
| 1 | `Smap_GetTileAtScreen` refuses an out-of-range read and returns 0 | the original indexes `pData[nStride*tileY + tileX]` with no check; past one wrap the coordinate is still negative |
| 2 | `StrategItem_ctor` takes the seed as a parameter | the original reads the global dword at `0x18640C`, which has three xrefs (two in `StrategList_AddItem @ 0x826e8`, this ctor) and no writer, so it is always zero |
| 3 | `Tilemap_GetAt` covers both globals | 0x82198 and 0x821BC are the same expression over `0x186B74` and `0x186C18` |

`StrategItem_AllocAdd` keeps the original's unconditional `PtrArray_Add`: a failed
allocation appends a null entry (0x415fc).

Still open in this cluster: `Smap_ZoomOut @ 0x81c08` / `Smap_ZoomIn @ 0x81e44` (they
rescale the scroll with double constants that need reading from the constant pool) and
the thin button wrappers `Smap_OnZoomOut @ 0x84ab8`, `Smap_OnZoomIn @ 0x84ad8`,
`Smap_BeginCraftSelectBtn @ 0x84634`.

### Zoom levels and the scroll rescale

`Smap_ZoomIn @ 0x81e44` and `Smap_ZoomOut @ 0x81c08`, ported as `Smap_ZoomIn` /
`Smap_ZoomOut` with the level table as data (`SmapZoomSet`, `SmapWorldSize`), asserted
by `test_strateg::TestSmapZoom` (20 checks).

Each step swaps three layer globals and takes the map size from the second one:

| level | tiles (0x186B04) | size source (0x186E40) | third layer (0x186BE0) | u16 pair at 0x186BC0 |
|---|---|---|---|---|
| 0 | 0x186B74 | 0x186B78 | 0x186C28 | 800 x 400 |
| 1 | 0x187064 | 0x1864F0 | 0x186C1C | 600 x 300 |
| 2 | 0x18705C | 0x1864EC | 0x186BB8 | 472 x 236 |

Level 2 has no zoom-in step and level 0 has no zoom-out step; both return without
touching anything (0x81E5C, 0x81C14/0x81C1C).

The scroll rescale is `itod` / `muld` / `dtoi` -- int to double, multiply by an
immediate, truncate back to int. The four factors, read out of the `muld` immediates:

| step | factor |
|---|---|
| in 0 -> 1 | 0.7501875468867217 |
| in 1 -> 2 | 0.7874015748031495 (exactly 1/1.27) |
| out 2 -> 1 | 1.27 |
| out 1 -> 0 | 1.33 |

**Zooming in from 0 to 1 is not the reciprocal of zooming out from 1 to 0**: 0.7501875468867217
against 1/1.33 = 0.7518796992481203. A full round trip on a scroll of 100 lands on 98.
That asymmetry is in the original, and the test asserts it rather than hiding it.

`Smap_ZoomIn` clamps `nScrollY` after the rescale using the **new** map height;
`Smap_ZoomOut` applies no clamp at all.

The clamp is a function of its own in the original, `Smap_ClampScrollY @ 0x837f8`:
it gates on the screen id at 0x186BC4 not being 2, reads the strip height from
`[0x186BBC]->uHeight`, and writes `160 - nMapH/2` for the lower bound, which is the
same value as `-(nMapH/2 - 160)` written the other way round. `Smap_ScrollBy @ 0x820d0`
(0x82124..0x82160) and `Smap_ZoomIn @ 0x81e44` (0x82080..0x820B0) both inline the
identical code instead of calling it, so the port keeps one public helper and three
call sites.

The two zoom buttons are `Smap_OnZoomOut @ 0x84ab8` and `Smap_OnZoomIn @ 0x84ad8`, two
instructions each: `Sound_Play(17)` and then a tail call on the one global view at
0x1863E8, with the zoom result returned straight through. The sample runs before the
zoom, so a refused step at level 2 or level 0 still clicks. Ported as `Smap_OnZoomIn` /
`Smap_OnZoomOut` with the sample behind a `SmapSampleFn` hook, asserted by
`test_strateg::TestZoomButtons` (12 checks).

### Deviations, continued

| # | Deviation | Reason |
|---|---|---|
| 4 | The level table is caller-owned data; the port does not write 0x186B04 / 0x186E40 / 0x186BE0 | the app reads `aSets[pView->nMode]` after a zoom instead of the port owning three globals |
| 5 | The `u16` pair at 0x186BC0 travels in `SmapWorldSize` | it is a neighbouring global, not part of the 36-byte view block |
| 6 | The strip height and the screen-id gate are arguments, not reads of `[0x186BBC]` and `0x186BC4` | the port owns no UI globals; the caller supplies both |
| 7 | Sample 17 is a `SmapSampleFn` hook | the audio backend is a seam in this port |

### Strategic accessors: per-base quantity, panel flags, unpause

Ported in `game/pufo_geoscape.{h,c}` and `game/pufo_clock.{h,c}`, asserted by
`test_geoscape::TestPanelFlagsAndQty` (12 checks).

`Base_GetItemQtyA @ 0x66198` and `Base_GetItemQtyB @ 0x661b8` are the same expression
over two object slots:

```
A -> pCtxBase.apObjects[113]->auPerBaseVal[nBase]
B -> pCtxBase.apObjects[112]->auPerBaseVal[nBase]
```

so the port keeps one `Base_GetItemQty(const ObjectRecFull *, i32)` and names the two
slots in the comment. The original's index is a `char` used with no bound check; an
index past the eight entries reads whatever follows. The port refuses and returns 0.

`Geoscape_SetPanelFlagA @ 0x8b448` and `Geoscape_SetPanelFlagB @ 0x8b478` each set one
byte of the block at `0x1864F4` (A writes `+1`, B writes `+3`) and only while the screen
id at `0x186BC4` is 0 or 2. For any other screen nothing at all is written -- that is the
whole function. Modelled as `GeoscapePanelFlags` with a shared `Geoscape_PanelApplies`.

`GameTime_Unpause @ 0x5d6c4` is one write: it clears the pause byte, the same
`apLines[106]` byte `GameTime_Pause @ 0x5d6ac` sets. There is no pause counter, so
unpausing while already running is a no-op rather than an error.

### Base lookup by type byte and the base item total

Ported in `game/pufo_geoscape.{h,c}`, asserted by `test_geoscape::TestBaseGridLookup`
(16 checks).

`Base_FindIndexByTypeByte @ 0x6ac30` is a linear search for the first base whose type
byte (`BaseRec.bUnk4 @ +0x04`) equals the requested type. With a base count of zero it
returns -1 immediately (0x6ac50). **Otherwise the loop has no bound at all** (0x6ac54):
it walks forward until some entry happens to match, off the end of the array if none
does. The port bounds it by the base count and returns -1.

`BaseGrid_GetByte0x654 @ 0x6acc0` and `BaseGrid_SetByteAtFound @ 0x6ac90` resolve a type
through that search and then read or write one byte of the grid the original keeps at
`gUiRootPanel.anLineAux[94]`. The getter returns the byte, the setter returns the found
index and writes nothing when there is none.

`BaseRec_AddItemQty @ 0x556e4` is two adds with no clamping and no floor at zero:

```
pItem->auPerBaseVal[(char)pBase->bSlot] += nDelta;
pBase->nStoresTotal                     += nDelta;
```

A negative delta drives both below zero, and the u16 slot wraps as u16 while the base
total is a signed int. Both are reproduced. The slot index is `BaseRec.bSlot @ +0x05`
widened as a char with no check; the port skips both adds when it falls outside the
eight entries.

## The save driver (done)

Files: `src/pufo/game/pufo_save.{h,c}`. Evidence: `SaveGame` @ 0x44988.

`SaveGame` is the only function that owns a slot file. It renders a frame, builds
`<dir>sav<n>.sav`, opens the slot as a 100-entry container, writes the chunks, and
closes. The full chunk order, measured from the port's own output and matching the
original's call sequence:

```
tactver  gamestate  bases  product  research  ships  smap  strateg  time
soldiers  stactflg  1_24scb                                  (always, 12 chunks)
mcnt  tgamedata  teamunits  aliens  diedsolds  hiddensold
flobjs  map  v124.dat  objbefore  rmp                        (tactical only, +11)
```

`stactflg` and `1_24scb` are part of the soldier write, and `flobjs` sits inside the
tactical half between `hiddensold` and `map`. Neither is obvious from the call list.

Four things in the driver are not in the chunk list at all:

- `Save_AssignRosterIndices` runs **between** `gamestate` and `bases`, not at the end.
  It is what gives every soldier its `nSaveId` and every tactical item its `nRefId`, so
  the `flobjs` links written later can be stored as indices.
- The mission counter `unk_138E8C` is **incremented at save time**, before `mcnt` is
  written, and the value stored is the incremented one. Saving a mission advances it.
- The `rmp` chunk has no writer function anywhere in the binary. Its seven writes are
  inline in `SaveGame`: count as 4 bytes, then `anModTileX`, `anModTileY`, `anModSizeX`,
  `anModSizeY` (240 each), the 1200-byte name block, and `anModLevel` last. 2404 bytes.
  That is the order `Save_ReadRmp` reads them in, and it is not the order `Map_LoadRmp`
  takes its arguments in.
- `nUiScreen` is cleared **only** in the tactical branch. Saving from the geoscape
  leaves the screen id alone.

`Save_AssignRosterIndices` reports false whenever the tactical flag is clear, and the
original ignores the result, so the driver deliberately discards it. Gating the save on
it would make every geoscape save fail.

Deviation: the original returns whatever `DatFile_Close` left in R0 and ignores every
write failure. The port returns false as soon as a step fails, and still closes the
file. The caller must run `DatFile_ctor` on the handle first; the original's handle is
a zero-initialised global, and skipping this leaves `nBufCap` uninitialised, which
makes the first chunk succeed and every later one fail.

## The tactical load (done)

Files: `src/pufo/game/pufo_save.{h,c}`. Evidence: `Save_ReadTGameData` @ 0x44248.

This is the reader for the tactical half, and it does four jobs beyond reading fields
back:

1. the 23 `tgamedata` fields, in the writer's order;
2. `Font_LoadTactSprites` (0x257c4, not ported, injected as a hook), then for each of the
   four sprite-set bytes at `Map+0x15116` that is not 23 it sets `unk_15827A[i]` from
   `unk_15829E` and loads the sprite set named `xcom_<byte>` (`aXcom0` @ 0xa7be8, stride 9)
   through `Font_LoadSpr` @ 0x461d8, then rebuilds the index with `Font_BuildIndex` @ 0x46480.
   A slot of 23 loads nothing **and leaves its flag word alone**;
3. `Save_ReadTeamUnits`, `Save_ReadFlobjs` and `Save_ReadMap`, in that order;
4. a tactical reset of about thirty fields in the Map block, which is what makes a loaded
   mission playable again.

The reset is a mix of clears and sets, and the sets are the part that is easy to miss.
Named by the original's `Map` offsets:

| Map offset | field | what happens |
|---|---|---|
| 0x01, 0x02 | two gap bytes | cleared |
| 0x04 | `nMultiTargetCount` | cleared |
| 0x2C | reached as a dword through `nUnk40+4` | cleared |
| 0x37 | `nAnimPhase` | cleared |
| 0x38 | `bPlayerInputEnabled` | **set to 1** |
| 0x3A | `nUiScreen` | cleared |
| 0x3C | `gap_60[0]` | cleared |
| 0x50..0x59 | the fire-status bytes | 0,1,2 cleared, 4..7 cleared as one dword, 8,9 cleared |
| 0x5A, 0x5B, 0x5C | `bGameTickHold`, `bEnemyTurnActive`, `bBattleEnded` | cleared |
| 0x60 | `apUiSlotsA` | slots 0,3,5,6,7,8,9 cleared; **slot 4 = 200** and **slot 10 = 10** |

`apUiSlotsA` is `void *[28]` at `Map+0x60`, yet slots 4 and 10 receive the small integers
200 and 10. Slot 8 is written as two half-writes, which is the same as clearing it. One
further write goes through a pointer: `unk_157198->bUnk70115 = 0`.

`v124.dat` is **optional**. When the chunk is present its two dwords are read into
`unk_138E6C` and `unk_138E70`. When it is absent they are **rolled**: `Game_RandU16(40) + 10`
and `Game_RandU16(20)`. A save without that chunk therefore gets a different value on every
load, and the test asserts both branches.

Deviation: the original checks none of the `tgamedata` reads, so a truncated chunk leaves
the fields at whatever they were. The port reports false when a read fails but still runs
the font, roster and reset work, because that is what makes the mission usable.

## The base facility grid and the workshop chain

Evidence: `Base_ClearFacilityGridRow @ 0x6a0e0`, `Base_CalcWorkshopCap @ 0x65894`,
`Base_CalcLabCap @ 0x65818`, `Base_CountActiveManuf @ 0x68264`,
`Base_CountActiveResearch @ 0x68198`, `Base_UpdateWorkshopNeeded @ 0x65bb4`,
`Base_UpdateLabNeeded @ 0x65c7c`, all in `src/pufo/game/pufo_geoscape.{h,c}`.

One base's facility layout is three 36-byte grids, indexed `6*column + row` over a 6x6
layout. The cell-type and needed-flag grids sit inside a **144-byte per-base block**; the
build-days grid sits outside it. The original's own types name them: IDA carries
`BaseFacilityGrid` (size 1524) as `buildDays u8[8][6][6]` at `+0x000` followed by
`fac BaseFacBlock[8]` at `+0x174`, and `BaseFacBlock` (size 144) as `tileA`, `tileB`,
`layer2` and `facType`, each a 36-byte `BaseFacLayer6`.

| grid | address | original name |
|---|---|---|
| build days | `grid + 36*nBaseIndex` | `buildDays[nBase]` |
| needed flags | `grid + 444 + 144*nBaseIndex` | `fac[nBase].layer2` |
| cell type | `grid + 480 + 144*nBaseIndex` | `fac[nBase].facType` |

The typed view and the raw offsets agree exactly: `Base_UpdateStoresNeeded` contains both
forms in one function, `pGrid + 144*nBase + 444` and `pGrid->fac[nBase].layer2`, and
`372 + 72` is `444`.

A cell counting as **free** means its build days are zero, that is nothing under
construction there, rather than that no facility owns the cell.

Cell type **2** is a lab, **3** is a workshop, **6** is storage, **4**, **5** and **9** are
the three states the site-state classifier looks at, and the classifier never looks at 2 or
3.

`Base_ClearFacilityGridRow` is **misnamed**: it clears nothing. It classifies a base and
stores a state byte into the per-base array at `0x161F38`, which is the port's
`GeoscapeWorld.apSiteState`. Any free type-4 slot gives state 1; any free type-5 slot gives
2, **overwriting** the 1, so the two are never both present; any free type-9 slot adds 10 and
exits, so the 10 lands at most once. A base with a free 9 slot ends up 10, 11 or 12. That
also settles what the original's `a2` argument is: **a base index**, not a set index.

`Base_CalcWorkshopCap` counts free workshop cells and returns `count * 50`. One free
workshop is worth exactly 50 units of capacity. It reads nothing but the grid.

`Base_CountActiveManuf` sums the first *N* u16 entries of a per-base quantity column, where
*N* is the single global byte at `0x160C4C` — shared by every base, read as an unsigned byte,
so 255 is the ceiling. The decompiler hides the address; the disassembly states it outright:

```asm
LDR  R1, =unk_159E98      base
MUL  R11, nBase, #0x190   400 bytes per base
MOV  R3, #0x4304          17156
LDRH R3, [R1,R3]          u16 entry; the loop advances R1 by 2
LDR  R2, =0x15EF08
LDRB R2, [R2, #0x1D44]    the count byte, at 0x160C4C
```

The column base is `0x159E98 + 17156 = 0x15E19C`, which is exactly where the product block
ends: the product block starts at `0x159E9C` and `PUFO_PRODUCT_BLOCK_BYTES` is 17152. The
constant is 17156 rather than 17152 only because the anchor `0x159E98` sits four bytes before
the product block. The columns start **immediately after the product block**, 400 bytes per
base, 200 u16 entries of which only *N* are read.

`Base_UpdateWorkshopNeeded` consumes both. Budget is `total / 50 + 1`, and 0 when the total
is 0, so any active manufacturing at all buys at least one needed workshop. For every
workshop cell it writes 1 into the needed grid while the budget lasts and 0 after it runs
out; non-workshop cells are never written. Visit order is row 0 across all six columns, then
row 1.

Two original quirks are reproduced rather than cleaned up. The call to `Base_CalcWorkshopCap`
at `0x65BC0` **discards** a pure function's result and the budget comes from
`Base_CountActiveManuf` instead — the port omits that call, which is the only deviation. And
the return value is loop-counter residue: the inner counter resets to 0 each row and always
reaches 6, so the function returns 6 no matter what it did.

### The lab chain is the same machine with a different kind

Evidence: `Base_CalcLabCap @ 0x65818`, `Base_CountActiveResearch @ 0x68198`,
`Base_UpdateLabNeeded @ 0x65c7c`.

`Base_CalcLabCap` is `Base_CalcWorkshopCap` over free cells of kind **2** instead of 3, still
`count * 50`. `Base_UpdateLabNeeded` is `Base_UpdateWorkshopNeeded` over kind 2, discarding
`Base_CalcLabCap` at `0x65C88` for the same reason and returning the same 6. The port keeps
one implementation of each — `FacilityCountFree`, `QuantitySumColumn`, `FacilityMarkNeeded`,
`FacilityBudget` — and gives every original its own name over it, because the originals are
literally copies of the same loop.

### All five needed passes, and what they say about the cell kinds

Evidence: `Base_UpdateLivingQuartersNeeded @ 0x65e24`, `Base_UpdateLabNeeded @ 0x65c7c`,
`Base_UpdateWorkshopNeeded @ 0x65bb4`, `Base_UpdateStoresNeeded @ 0x65d44`,
`Base_UpdatePsiLabNeeded @ 0x65fc8`.

The original has exactly five of these and the port now has all five. They are one machine;
only the cell kind and the divisor change:

| pass | kind | divisor | budget total comes from |
|---|---|---|---|
| `Base_UpdateLivingQuartersNeeded` | 1 | 50 | `CalcOccupiedSpace` + `CountOutboundTransferSpace` |
| `Base_UpdateLabNeeded` | 2 | 50 | `CountActiveResearch` |
| `Base_UpdateWorkshopNeeded` | 3 | 50 | `CountActiveManuf` |
| `Base_UpdateStoresNeeded` | 6 | 300 | `CalcStorageUsed` + `CountItemQtyAcrossBases` |
| `Base_UpdatePsiLabNeeded` | 8 | 10 | `CountPsiSoldiers` |

That table is also the cell-kind map for `fac[nBase].facType`: 1 living quarters, 2 lab,
3 workshop, 6 stores, 8 psi lab, plus 4, 5 and 9 which the site-state classifier reads.

Stores is the only pass needing six times the pressure of the 50 group. Psi lab is the most
sensitive: one psi soldier claims a cell.

Each pass discards its own `Base_Calc*Cap` call, and each returns the loop-counter residue of
6. The budget totals come from functions not yet ported, so the port takes each total as an
argument, exactly as it does for the quantity columns.

Clearing all five removes a blocker shared by `LoadGame @ 0x44bbc` and
`Geoscape_CloseUI @ 0x8b94c`.

`Base_CountActiveResearch` reads the **same** count byte as manufacturing, `0x160C4C`, so one
entry count serves both, but its column has a different stride:

```asm
LDR  R2, =0x15EF08
LDRB R3, [R2, #0x1D44]    the count byte, at 0x160C4C
MUL  R11, nBase, #0x120   288 bytes per base, not 400
MOV  R2, #0x1324          4900
LDRH R2, [R1,R2]          u16 entry; R1 advances by 2
```

so its column base is `0x15EF08 + 4900 = 0x16022C`, 288 bytes per base, 144 u16 entries.

**Open question, recorded rather than guessed.** The manufacturing columns run 3200 bytes
from `0x15E19C` and end at `0x160DDC`, and `0x16022C` falls inside that range. Two arrays
with different strides cannot both own that memory, so these are most likely two views of one
block rather than two separate arrays. Nothing in the port depends on the answer: both
functions take the column as an argument.

## The mission outcome table inside `Tact_Shutdown` (mapped, not yet ported)

`Tact_Shutdown (0x3d568)` (3788 bytes) is the tactical teardown and one of the three remaining
`LoadGame` blockers. Its teardown half is mechanical. Its interesting half is at 0x3DCF4: when
`bKeepSome == 0` the function commits the mission result, and that is a decision table over two
bytes the IDB had never explained.

- `unk_128A8C` — the mission ended in a **retreat**, not a completed landing.
- `unk_128AD5` — the outcome code. **0, 2 and 3 all mean the craft leaves**; **1 means the site is
  held**.
- `unk_1153D9` — selects which of the two outcome paths runs.

With `unk_1153D9` set: outcome 1 demolishes the base facility named by the byte at 0x159D81;
outcome 0 removes the current strateg item when its `nType` is 4.

With `unk_1153D9` clear: outcomes 0, 2 and 3 set byte **0x162028 to 1** — that is the one-shot
replacement-spawn gate `StrategList_RemoveAt @ 0x82ab0` reads at 0x82AB0, so leaving a mission is
what re-arms the next UFO spawn. Then either the item is removed, or (when `unk_128A8C == 0`) the
item is removed only if its `nType` is 4 **and** `Ship_RemoveWithCargo` runs for the craft at
`Map_nCraftIndex`. Outcome 1 transfers tactical loot into the base on a retreat and demolishes the
facility otherwise.

**Unit removal runs before the outcome** (0x3DD0C): every enemy is removed with no test; civilians
are removed when `Soldier+960` is 0; XCOM units are removed when `nUnitType` (byte at `Soldier+160`)
is `> 3 && <= 8` **or** `Soldier+960` is 0. The loop bound is re-read from `nCivilianUnitCount` /
`nXcomUnitCount` every iteration because `Soldier_RemoveById` shrinks the list — the walk is against
a moving target.

`Tact_Shutdown` also frees the tactical bank through **thirteen pad families** (0x3D6E4): group
sizes 19, 13, 12, 29, 7, 9, 9, 3, 3, 1, 8, 1, 1 at widget offsets 0, 19, 32, 60, 53, 44, 89, 98,
101, 104, 105, 128, 129. Hex-Rays again renders the inner step as four `UiControl`s where the ARM
means four bytes, so each group covers three adjacent widgets — the same misreading as
`StartUi_Shutdown @ 0x38d7c`.

Five callees are still unported: `Map_FreeRmpNodes` (0x55018), `Soldier_RemoveById` (0x36b8c),
`Base_DemolishFacility` (0x66b70), `Ship_RemoveWithCargo` (0x36830), `Base_TransferTacticalLoot`
(0x68344). The whole decode is recorded in the IDB.

## Losing a craft: `Ship_RemoveWithCargo @ 0x36830` (done)

Ported as `Ship_RemoveWithCargo(const ShipRemovalEnv *, i32 nShipId)` in `game/pufo_ship.{h,c}`.
It is what runs when a mission ends with the craft gone — `Tact_Shutdown`'s outcome path and
`Base_DemolishFacility` both reach it.

**The ship array is compacted by struct copy, not by moving pointers.** ARM at 0x36A50 does
`memmove(apCraft[i-1], apCraft[i], 0x44)` — 68 bytes, `sizeof(Ship)`. The pointer array keeps its
pointers; only the last object is ever deleted, at 0x36B40 after the count drops.

**The cargo column moves with the ships.** ARM at 0x36A24 computes `2*i + 0x38` for the store and
reads at `2*i + 0x3A`, so `ObjectRecFull.aItemQty[i-1] = aItemQty[i]` for every object record.
0x3A is the same `aItemQty` base the ships chunk writes cargo through, and `nObjectCount` is the
u16 at 0x114A60.

**Three index sets are decremented**, because three things index the ship array positionally:
`Soldier.bUnk183 @ 0x0B7` (the soldier's ship, left alone when it is 254 or 255 — the two
"not assigned" sentinels), the crew list itself (every soldier on the doomed craft is deleted),
and the assigned-ship array at 0x16146C with its signed count at 0x161468. The last one has **no
sentinel test**, so a stored 255 becomes 254.

**The hangar flag** is cleared at `0x158CD8 + 6*(24*bHomeBase + bHangarY + 12) + bHangarX`
(0x368CC..0x368D8): six bytes per cell, 24 cells per base, and the three bytes feeding the index
are the home base and the two hangar coordinates.

Finally `Ship.nSlotIdx @ 0x1C` is renumbered 0..count-1 for every remaining craft — the same value
`Ship_ctor` assigns at creation.

### A real bug this port exposed in `PtrArray_Remove`

Removing the *last* item of a `PtrArray` crashed the new test. The branch was

```c
if (nIndex + nRemove >= nTotal) { apNew = NULL; }
```

which threw the whole array away whenever the removal reached the last slot: every item before it
leaked, and the struct was left with a NULL `apItems` and a non-zero `nCount`. The condition is
now `nTotal - nRemove <= 0` — only an empty result drops the array. Fixed in
`util/pufo_ptrarray.c`; the memmove logic already handled the prefix correctly.

Covered by `tests/test_ship.c` (`TestShipRemoveWithCargo`, 22 checks).

## `Base_DemolishFacility @ 0x66b70` removes a whole base (mapped, not yet ported)

The name is misleading in the same way `Base_ClearFacilityGridRow` is. It does not demolish one
facility inside a base. It finds the base whose 64-byte record at **0x186C40** has `nFacType` equal
to the argument, and then removes that base entirely: its strateg entry, its stationed soldiers,
every craft homed there, and finally `apBases[count-1]`.

The base count it walks is the byte at `pBase + 1560`, which with `pBase = 0x159768` is **0x159D80**
— the same byte the bases chunk stores, `BaseRec_ctor` indexes `auPerBaseVal` with, and
`GeoscapeUi_FreeAll` gates its base loop on.

**The argument is used with two meanings.** It is matched against `0x186C40[i].nFacType` to find the
base, and against `StrategItem.nSiteId` (with `nType == 0 && bActive == 0`) to find the geoscape
entry. If no entry matches, the index equals the item count and `StrategList_RemoveAt` is called out
of range.

**Who gets deleted with the base:** soldiers with `Soldier.nKind == baseIndex` **and**
`bUnk183 > 0xFD` — the stationed ones, not the ones aboard a craft. Then every ship with
`bHomeBase == baseIndex` goes through `Ship_RemoveWithCargo`, re-reading the ship count each time
because the array shrinks under it. Then the `SmapSite` records at 0x162030 (count at 0x186410) with
`bFlag1 == the argument` get `bFlag0 = -1`, the not-assigned marker.

**The compaction is the interesting part**: seven parallel per-base arrays shift left at once, each
with its own stride and its own copy idiom.

| array | per-base stride | how it is moved |
|---|---|---|
| `0x186C40` facility records | 64 | byte-by-byte loop, 64 iterations (0x670D4) |
| `apBases` records | 20 | `memmove(..., 0x14)` (0x66F44) |
| facility block `0x159768.fac` | 144 | 4 × 6 cells (0x66F4C) |
| `buildDays` grid `0x159768 + 36*base` | 36 | 6 × 6 bytes (0x66FA8) |
| research column `0x16022C` | 144 | gated on the shared count byte at `0x160C4C` |
| product columns `0x159E98` | — | three word/dword shifts per step (0x67078..0x670BC) |
| `ObjectRec.auPerBaseVal` | 2 per base | shifted in every object record (0x66F0C) |

The research column's 144-byte stride and the `0x160C4C` gate are the same pair
`Base_CountActiveResearch` uses, seen here from the other side.

Recorded in the IDB. Porting it needs the port to own all seven arrays at once, which is the next
step after `Geoscape_InitUI`.

## The cargo hold: what fills it and what counts against it (done, plus the chain above it mapped)

`Craft_GetTypeCargoCap @ 0x65ac4` and `Base_CalcWeaponStorageUsed @ 0x65b30` are ported into
`game/pufo_ship.{h,c}`. They are the two leaves under the craft-cargo chain.

**The hold cap is chosen by `Ship.bCraftType @ 0x02` and nothing else:** 0 → 140, 1 → 120,
2 → 260, 3 or 4 → 0, anything else → **-1**. The -1 is not a cap; a caller comparing against it
gets a hold nothing fits into.

**What counts against the cap** is `ObjectRecFull.bCargoSize @ 0xC6 × aItemQty[craftIndex]`, summed
over a filtered index range. The ARM states the filter literally at 0x65B74:

```
(index > 0 && index < 25) || (index > 35 && index < 52)
```

so **objects 1..24 (the weapons) and 36..51 (the ammo) fill the hold**, and object 0, 25..35 and 52
are never charged. The first half of the condition looks like a pointer compare against the Map base
only because the compiler walks `apObjects` as `Map + 4*index`.

That byte at 0xC6 was `gap_B9[13]` in one place and `+198` in another depending on which base
register the code used. It is now named `bCargoSize` in `ObjectRecFull`.

`apObjects` sits at **Map + 0x108**, not 0x120 — an earlier note here said 0x120 and was wrong.
The ARM settles it: `Craft_LoadStoresFromBase` loads `R4 = apCraft - 0x10AC`, which is just `&Map`,
then does `ADD R3, R4, R2, LSL#2` and `LDR R3, [R3, #0x108]` — that is `Map + 4*nIndex + 0x108`, so
index 0 of `apObjects` is at 0x108. `Base_CraftWeaponItemId` agrees from the other side: its arms
read `[R0, #0x1E8]`, `#0x1EC`, `#0x1F0` and `#0x2AC` for object records 56, 57, 58 and 105, and
488 - 4*56 = 264 = 0x108.

The three arrays sit side by side: `apObjects` at **Map + 0x108** (256 pointers), `apCraft` at
**Map + 0x10AC**, `apBases` at **Map + 0x1174**. The folded constants that look like large absolute
addresses (`4*nSrc + 1128888`, `4*(1067 + nSrc)`) are the compiler folding the Map base into the
index — 4*1067 = 4268 = 0x10AC, which is why `apCraft[n]` appears as an offset of 1067 dwords.

### The chain above them (mapped in the IDB, not yet ported)

`Base_TransferTacticalLoot` (0x68344) → `Base_StripAllCraftCargo @ 0x69a38` →
`Craft_LoadStoresFromBase @ 0x693ec` → the two functions above.

`Base_StripAllCraftCargo` runs over every craft with `Ship.bCanDogfight @ 0x21` set and, for ten
groups, dumps the craft's cargo into its home base and refills it. The gate is always
*craft-carried < base-stock*, and the pairs are cargo object vs weapon object:

| cargo | weapon | | cargo | weapon |
|---|---|---|---|---|
| 36 | 1 | | 43+44+45 | 8 |
| 37 | 3 | | 46+47+48 | 10 |
| 38 | 4 | | 49 | 11 |
| 39 | 6 | | 50 | 12 |
| 40+41+42 | 7 | | 51 | 13 |

`Craft_LoadStoresFromBase` then refills one unit at a time, each unit gated on
`Base_CalcWeaponStorageUsed + bCargoSize < Craft_GetTypeCargoCap`. For kinds 7, 8 and 10 it splits
the wanted load across three ammo pools in order: `n1 = min(want, s1)`, `n2 = min(want-n1, s2)`,
`n3 = min(want-n1-n2, s3)`.

Covered by `tests/test_ship.c` (`TestCargoHold`, 14 checks).

`Craft_LoadStoresFromBase @ 0x693ec` is now ported beside them (`Craft_LoadStoresFromBase` in
`game/pufo_ship.{h,c}`): it takes `min(craft carried, base stock)` for a single ammo object, or
splits the wanted load across the three-pool sets 40/41/42, 43/44/45 and 46/47/48 for kinds 7, 8
and 10, and re-reads the hold capacity before every unit so a load that fills the hold stops early.
That leaves `Base_StripAllCraftCargo` (0x69a38) as the only unported step under
`Base_TransferTacticalLoot` (0x68344). Covered by `TestLoadStores`, 12 checks.

`Base_StripAllCraftCargo @ 0x69a38` is ported as `Base_StripAllCraftCargo` in `game/pufo_ship.c`,
driven by a ten-row table `g_aCraftAmmoGroups` instead of ten copies of the same three steps. The
net effect of a group is that the craft ends up carrying one round per weapon it has, as far as the
base can supply them. `Base_TransferTacticalLoot` (0x68344) is now the only step left under it.
Covered by `TestStripCraftCargo`, 12 checks.

### The rest of the base capacity family (done)

`Base_CalcLivingQuartersCap @ 0x6579c`, `Base_CalcStoresCapacity @ 0x65720`,
`Base_CalcAlienContainmentCap @ 0x65910` and `Base_CalcHangarCap @ 0x6611c` are one line each over
the shared `FacilityScoreKind` walk: kind 1 at 50, kind 6 at 300, kind 7 at 10, kind 11 at 1. The
weights are the capacities — a living-quarters cell houses 50 people, a stores cell holds 300 units,
a containment cell holds 10 aliens, a hangar cell is one craft slot.

`Base_HasResearchSlotFree @ 0x662bc` and `Base_HasWorkshopSlotFree @ 0x66340` are **not** searches
for a free cell: they compute the whole capacity and return `nNeeded <= capacity`, so they are
`Base_CalcLabCap` / `Base_CalcWorkshopCap` with a threshold compare bolted on.

`Base_CountBuiltFacilities @ 0x682c8` is the only member that **ignores build days** — kinds 13, 14,
15 and 16 are weighted 500, 600, 900 and 1500 whether or not they are finished. The shared walk
grew a `bRequireFree` flag for it rather than copying the loop a fifth time. Whatever this measures,
it is not a capacity.

Covered by `TestCapacityFamily`, 14 checks.

`Base_CountShipsAtBase @ 0x66090` is ported, and the arity question it raised is settled:
`BaseGrid_GetByte0x654 @ 0x6acc0` takes **two** arguments (its first is never read) and reaches both
the grid and the base array through globals — the port's `BaseGrid_GetTypeByte` takes those same two
things as parameters.

The function is a craft count plus a grid byte: `count(bHomeBase == n) + grid[find(n)]`, where `find`
searches for `apBases[n]->bUnk4`. Because the type byte comes from the base itself, the search always
answers at least that base, so the grid half is never skipped by a failed search — only an empty base
slot skips it, and there the port returns the craft count instead of dereferencing NULL.

Covered by `TestShipsAtBase`, 6 checks.

### The overlay layer of each zoom set, and the mirror lookup (done)

`Smap_GetTileAtMode @ 0x821c4` is ported as `Smap_GetOverlayAtMode`. It reads the **overlay**
layer, not the terrain layer `Smap_GetTileAtScreen` reads. The three globals it switches between
(`0x186C28`, `0x186C1C`, `0x186BB8`) hold `PadImage` **pointers**, and they are the third PadImage of
each zoom set — so each zoom level has a terrain grid and an overlay grid, and the port takes the
three overlays as an array.

Two details that are easy to get wrong: `x` is reduced by a global origin (`0x186C20`) before the
lookup, and the stride is the PadImage's own `uWidth`, not the viewport's `nMapW`. For a mode outside
0..2 the original returns an uninitialised stack byte; the port returns 0.

`Ship_FindIndexByStrategType @ 0x5a92c` is the **mirror** of `StrategList_FindIdxForShip @ 0x6a05c`:
that one takes a ship index and returns the strateg index, this one takes a strateg index and returns
the ship index whose `Ship.bId` equals the item's `nDisplayId`. Like its mirror it gates on nothing
else — not item type, not active state. It dereferences `PtrArray_Get(nStrategIdx)` with no bounds
test; the port returns -1.

Covered by `TestFindByStrategIdx` and `TestOverlayAtMode`, 14 checks.

### Craft stores slots 31..35, and the byte that says which base you are looking at (done)

`Base_CraftWeaponItemId @ 0x6641c` is the selected base's stock of the item a craft stores slot
refers to:

| slot | object record | value read |
|---|---|---|
| 31 | `apObjects[56]` | `auPerBaseVal[col]` |
| 32 | `apObjects[57]` | `auPerBaseVal[col]` |
| 33 | — | the constant **1000**, no record touched |
| 34 | `apObjects[105]` | `auPerBaseVal[col]` |
| 35 | `apObjects[58]` | `auPerBaseVal[col]` |
| other | — | **-1** |

The column comes from the call site: at `0x7ADC4` the only code caller (`BasePlaneScreen_CommitStores
@ 0x7ac38`) passes it as the **signed global byte `0x159D81`** — the byte sitting immediately after
the base count at `0x159D80`. That byte is the base the base screens are currently showing, so this
function is not about a craft at all; it asks how much of the item the *displayed* base has.

Slot 33 returning a fixed 1000 is the odd one: whatever that slot holds, the original keeps no
per-base count for it.

Covered by `TestCraftWeaponItemId`, 13 checks.

## Parity report integrity: five citations worth 29 KB were not ports

`audit_names.py` now prints the byte weight of every flagged citation, sorted by size. That
immediately exposed five citations that credit work the port never did — **29,076 bytes, 4.9% of the
binary**:

| original | bytes | what the citation actually said |
|---|---|---|
| `Unit_CanStepTo` `0x27bb8` | 14,716 | "predicate only… the real function is ~14.7 KB and is its own body of work" |
| `Tact_CheckBattleEnd` `0x34fbc` | 5,748 | a hook field: "the seam is explicit rather than a stub" |
| `Tact_InitMission` `0x3c550` | 4,120 | two comments describing what the original reads from lightmap.dat |
| `BeforeEnemyStep` `0x4b9b8` | 2,792 | "injected via `pfnBeforeEnemyStep` because both need the full world" |
| `ResearchScreen_RebuildTopicList` `0x74270` | 1,700 | naming the original caller that proves a type alias |

Every one of them said, in the same sentence, that the port does not implement it. All five are now
written `Name (0xaddr)`, which the tool does not credit.

Honest coverage after the correction: **598 / 1103 functions (54.2%)**, **194,100 / 598,792 bytes
(32.4%)**.

`Map_ComputeReachable @ 0x2b820` was checked and left alone — it is ported as `Path_ComputeReachable`,
a documented rename.

### Second integrity batch: eight more citations removed (9,480 bytes)

Checked by hand against the source, one citation site at a time:

| original | bytes | verdict |
|---|---|---|
| `TactHud_DrawSelectedUnitPanel` `0x1d5ec` | 1,484 | cited only as "all six callers are …" — no such function in the port |
| `AfterEnemyStep` `0x4c4a0` | 1,368 | hook field: "`pfnAfterEnemyStep` stands in for" |
| `Map_InitAndLoadRegistry` `0x22648` | 1,284 | cited as the source of default values `Options_InitDefaults` borrows |
| `MapView_DrawIsoLayer` `0x21e2c` | 1,252 | cited describing which coordinate family the original calls |
| `MapView_DrawUnitHeightGlyph` `0x1c240` | 1,132 | cited describing what the original reads; the citation also spanned two lines |
| `Tact_HandleMapClick` `0x1512c` | 1,104 | **not a function citation at all** — a struct field comment `nCursorY @ 0x1512c` naming a global |
| `Map_ProbeStepOccupants` `0x2778c` | 1,068 | cited listing which originals read a cell field |
| `Tact_UpdateUnitControl` `0x4a444` | 788 | only the unit-arithmetic half is ported, as `Unit_ApplyControlRecovery` |

The `0x1512c` case is a new shape of the same problem: a **data** address written in the `@` form is
credited as a recreated function. Struct-field comments that name globals must not use `@`.

Checked and **left alone** as documented renames: `Assets_LoadStartup` → `Boot_LoadStartup`,
`Map_LoadRmp` → `Rmp_Load`, `Soldier_Generate` → `Unit_Generate`,
`Map_ApplyTemplateToCells` → `MapTemplate_ApplyToCells`, `UiPanelMsg_Draw` → `UiPanel_DrawText`,
`Map_ComputeReachable` → `Path_ComputeReachable`, `Ship_ctor` → `Ship_Ctor`.

Honest coverage now: **590 / 1103 functions (53.5%)**, **184,620 / 598,792 bytes (30.8%)**.

## The rename ledger, and a tool fix it exposed

`docs/parity/renames.tsv` now records every original name the port keeps under a different name —
87 pairs, each checked in the source. `audit_names.py` consults it, so a flag is either an explained
rename or a real gap instead of 200-odd lines nobody reads.

**133 flags remain, worth 8,728 bytes (1.5% of the binary).** The largest is `Soldier_RemoveById
@ 0x36b8c` (204 bytes): the port has `Unit_RemoveAt`, which is the compaction half, and has no
equivalent of the search-by-id half. That one is a genuine partial and is deliberately left in the
flag list rather than papered over with a rename entry.

`audit_credits.py` had a bug the ledger work exposed: it searched the whole line for a denial, so on
`Tact_SelectNextUnit @ 0x17304, Tact_SelectUnit (0x2c6c4, not ported)` it flagged the first function
too. Denials now bind to the clause around the citation they modify, bounded by commas and
semicolons. NEGATED citations after the fix: **0**.

Renames added this pass, all verified against a definition in `src/pufo`: `Map_LoadMa1` ->
`MapTemplate_Load`, `MapView_FocusOnUnit` -> `MapView_FocusOnCell`, `Tact_CycleNextUnit` ->
`Roster_CycleNext`, `WinMain` -> `wWinMain`, `EnemyAi_bMultiTargetTypeMatch` ->
`EnemyAi_MultiTargetTypeMatch`, `Smap_GetTileAtMode` -> `Smap_GetOverlayAtMode`, the six
`UiPanel_*Line*` functions -> `UiLineList_*`, `Tact_TryFireMode4/10/5` -> `Ui_TryFireMode`,
`Tact_OpenLeftHandMenu`/`Tact_OpenRightHandMenu` -> `Ui_OpenHandMenu`,
`Tact_RemoveUnitFromSideLists` -> `SideLists_RemoveUnit`, and the five render `*_Inner` blitters.

`Tact_SelectUnit @ 0x2c6c4` (148 bytes) and `Tact_LoadLoftData @ 0x3bd6c` (184 bytes) were cited in
module Evidence headers with no port behind them; both are now parenthesized.

Correction to my own arithmetic: that batch was first written as 8,692 bytes. The measured drop was
598 -> 590 functions and 194,100 -> 184,620 bytes, which is 9,480. The eight sizes
(1484 + 1368 + 1284 + 1252 + 1132 + 1104 + 1068 + 788) add to 9,480. The whole integrity pass was
15 citations and 38,888 bytes: 29,076 + 9,480 + 332.

### `Soldier_RemoveById @ 0x36b8c` closed, and it was the flag worth the most (done)

The one partial I left in the flag list on purpose is now a real port: the search half
(`Soldier.nUnk440 @ 0x1b8`) sits on top of the existing `Unit_RemoveAt`, which already did the
delete, compact and decrement.

The original's bug is worth stating plainly. Its search loop ends with the index **at the count**
when nothing matches, and `0x36BE0` then does `operator delete(apSoldiers[that index])` with no
found test — one slot past the array — and decrements the count anyway. A failed lookup therefore
destroys a slot the roster never had and shortens the list. The port returns the list untouched.

Unexplained-flag bytes dropped from 8,728 to **8,524 (1.4% of the binary)**; coverage is unchanged
at 588 / 1103 because this citation was already being credited, dishonestly, by `Unit_RemoveAt`.
That is the whole point of the flag list: the number was already counting this function, and the
flag was the thing that said it should not be.

Covered by `TestRemoveById`, 9 checks.

### A craft's crew count, and what the hangar screen is about to load (done)

`Base_CountSoldiersAtBase @ 0x6649c` is renamed in the IDB to **`Craft_CountSoldiersAboard`**. The
name was wrong. It counts soldiers whose `Soldier.bUnk183` equals the argument, and `bUnk183` is the
craft a soldier is assigned to — the same byte whose 254 and 255 sentinels `Ship_RemoveWithCargo`
leaves alone. At the call site in `BasePlaneScreen_CommitStores` (`0x7AD38`) the argument is
`unk_161AC4`, which that function also passes as `nCraftIdx` to `Craft_GetTypeCargoCap` and
`Base_CalcWeaponStorageUsed`. It is a craft index, not a base index.

There is no sentinel test, so asking for "craft 255" counts the soldiers who are aboard nothing.
That is the original's behaviour and the port keeps it.

`BasePlane_CalcWeaponStoresUsed @ 0x78e78` is the other half: it sums `bCargoSize * carried` over
**every** object index, where the carried counts are what the hangar screen accumulated in
`unk_161AD0` — the same array `BasePlaneScreen_CommitStores` increments at `0x7AFD8` for each row it
accepts. Unlike `Base_CalcWeaponStorageUsed`, it does not filter indices: index 0 counts. Filtering
already happened when the counts were recorded.

`BasePlaneScreen_CommitStores` adds the two together with `apObjects[slot].bCargoSize` and compares
against `Craft_GetTypeCargoCap`. Its own port is blocked on modelling the hangar screen's state
block (`0x161A40`, `0x161AC8`, `0x161AD0`, `0x161EE0`, `0x161EEC`, `0x161EF4`); the full decode, including
the crew quota per craft class and the slot-34 asymmetry (5 leaves the base, 50 lands on the craft),
is recorded in the IDB.

Covered by `TestCrewAndStoresTotals`, 10 checks.

### What a base actually costs each month (done)

`Base_SumCraftAtBase @ 0x6a230` is renamed in the IDB to **`Base_CalcMonthlyUpkeep`**. The old name
describes one of its five terms. It is the monthly cost of one base:

| term | cost |
|---|---|
| craft homed here | type 0 or 1 → 500,000; 2 → 700,000; 3 → 600,000; 4 → 400,000; anything else → nothing |
| soldiers assigned to the base | 20,000 each |
| object 112 stock at this base | 20,000 per unit |
| object 113 stock at this base | 30,000 per unit |
| geoscape sites with `bFlag1` == this base | `20,000 * (auItemQty[224] + bRecoveredSoldierAssign) + 30,000 * auItemQty[226]` |
| facility cells | **50,000 for every cell whose type byte is not 255** |

Two facts that only show up when you write the test:

**An empty facility cell is 255, not 0.** A zeroed type block reads as 36 built facilities and costs
1,800,000 a month. The first version of this test asserted against a zeroed grid and "failed" against
a correct port.

**This term reads the facility TYPE grid and never looks at build days**, so a facility still under
construction is charged the full 50,000. That is the opposite of every capacity function in the same
family, which all require build days to be zero before a cell counts.

Also ported: `Base_CalcPsiLabCap @ 0x6598c` (kind 8, 10 per free cell — Hex-Rays scrambles it into a
do/while over a running pointer, but the two grids are the usual pair) and `Base_CountSoldiers
@ 0x661d8` (the plain base-assignment count, versus `Base_CountSoldiersAssigned @ 0x66230` which also
asks whether the soldier is reachable).

These are the two callees `Geoscape_CloseUI` case 19 needs for the monthly economy close. That case
is now reachable except for `PsiScreen_Init`, `EndScreen_Init` and the funding totals block.

Covered by `TestPsiCapAndUpkeep`, 9 checks.

### Sending a craft home, and the one number that separates the two ways to do it (done)

`Ship_RetargetTravel @ 0x5a484` and `Ship_ClearWaypointIntercept @ 0x5a830` are the same body with
one difference, and the difference is a single value written to the same field:

| step | `Ship_RetargetTravel` | `Ship_ClearWaypointIntercept` |
|---|---|---|
| drop a type-5 waypoint the craft is chasing | yes | yes |
| find the craft's own entry | yes | yes |
| write `nTimer` (+0x2C) | **0** — start counting again | **0xFFFF** — parked, cannot fire |
| travel to the home base | yes | **no** |

A waypoint is `nType 5`, and it is *removed* from the strateg list rather than merely unlinked: it is
a one-shot contact, so leaving it in the list would let another craft pick it up.

`Ship_ClearInterceptByStrategIdx @ 0x5a5b8` is the cleanup a removed contact needs: retarget every
craft whose `bHomeBase < 0xFE`, `bIntercepting` is set, and `nStrategIdx` is the item. It re-reads the
ship count after each retarget, because `StrategList_RemoveAt` can spawn a replacement craft mid-loop.

`BaseRec_Create @ 0x32c88` allocates a 20-byte base record, runs `BaseRec_Ctor`, and stores it at
`apBases[nBaseCount]` — it does **not** increment the count, the caller does, and a failed allocation
stores the null anyway.

**A duplicate found in the original.** `Base_FindIndexBySiteId @ 0x5a8d0` is a second copy of
`Base_FindIndexByTypeByte @ 0x6ac30`: same search, same `BaseRec.bUnk4`, same -1. The only difference
is that this copy bounds its loop and answers -1 on an empty list, while 0x6ac30 walks off the end. The
port keeps one implementation and does not cite 0x5a8d0.

**A fixture bug that exposed a design problem.** The port carries the strateg list as a `PtrArray`
(the shape `StrategList_RemoveAt` wants) and searches it as a `StrategList` (the shape
`StrategList_FindByShipId` wants). Those two structs have their fields in the opposite order, so they
cannot alias. Storing a second view in the env went stale across a removal and produced a wrong lookup.
The view is now built fresh from the array on demand — one helper, no stored duplicate.

Covered by `TestInterceptRetarget`, 10 checks.

### The two funding leaf helpers, and a band that can never be reached (done)

`Funding_ScoreToBarLevel @ 0x5aa60` — the first argument is not an amount and not an index into five
equal slots. It maps 0 → 3, 1 → 5, 2 → 4, 3 → 1, 4 → 3, and **two scores share level 3**. Direction
-2/-1/1/2 means -50,000 / -25,000 / +25,000 / +50,000; direction 0 or anything outside -2..2 returns 0
and throws the level away. Hex-Rays renders the level as `v2 << 24 >> 24`, which is just the compiler
widening a signed char.

`Funding_DeltaToArrow @ 0x5ab34` — which arrow to draw, -2..2, against thresholds derived from the
total (`total/8` and `total/2`, integer division). The third test reads `total / -8`, not `total / 8`:
dividing by -8 is what puts the middle band on the other side of zero.

The consequence is worth stating, because it is not obvious from the source: **with a positive total
only -2 and 2 are reachable.** The first test swallows everything at or below `total/8 + total/2`, and
the -1 band would need a delta above that which is also at or below `total/8` — impossible. With a
negative total all five arrows become reachable. A base in the black never gets a "flat" or "up some"
arrow no matter what its delta is.

`FundingScreen_Open @ 0x5b560` and `FundingScreen_OpenMonthly @ 0x5d160` were decoded and left
unported: each tail-calls a 1.2–1.5 KB screen builder (`FundingScreen_Toggle`, `FundingScreen_Init`)
that is not ported yet.

Covered by `TestFundingHelpers`, 16 checks.

### `BaseGrid_SubByte0x654 @ 0x6acec` — a guard that is not a floor (done)

It resolves a type byte to a base index and subtracts from that base's grid byte. Two details:

- It only subtracts when the byte is **already non-zero**, so a zero stays zero.
- There is **no floor**, so a positive byte wraps below zero. 30 minus 40 gives 246.

It also does not check the found index: `Base_FindIndexByTypeByte` answers -1 when no base has the
type, and `0x6AD1C` then reads and writes `abGrid[-1]`. The port refuses. Its return value is the
constant 1620 — register residue, not a status — so the port returns the index instead.

Three small functions decoded and left unported, with the decode recorded in the IDB:
`Smap_ClearSelection @ 0x84648` (clears the selection and unpauses **only if no message lines are
left**), `OptionsScreen_PanelPrev/Next @ 0x6d01c/0x6d064` (scroll four panels' cursors at once), and
`Ufopaedia_CacheSelRow @ 0x99fd0` (a one-shot cache: the selected row is captured the first time and
never refreshed by this path).

Covered by the extended `TestBaseGridLookup`, 6 more checks.

## The landing pass: `Base_TransferTacticalLoot @ 0x68344` (done)

3,048 bytes, 87 basic blocks, and every callee is now ported. It walks every base and every craft,
and for each craft that is at its home base with its weapons hot it parks it in a hangar cell,
credits its crew, dumps and refills cargo, checks the craft is not carrying more weapons than the
base has ammo for, reloads its stores, and re-draws each mount's ammo. The gate at 0x683DC clears
`Ship.bWeaponsHot` as it tests it, so a craft is processed once per landing.

Four things this function settled for the whole cargo family:

**`anAmmoTypeId[3]` at `ObjectRec + 0xA8` holds object indices, not ammo type ids.** The shortage
loop at 0x68658 reads `apObjects[obj->anAmmoTypeId[k]]->aItemQty[shipIdx]` and compares the sum
against how many of that weapon the craft carries. Objects 1..13 are the ones checked.

**Slot 34 is asymmetric, and now we know why.** The base is charged 5 per unit and the craft is
credited 50. `BasePlaneScreen_CommitStores` shows the same asymmetry on the manual path; this is
the landing path, so it is not a hangar-screen quirk.

**Two mounts of the same weapon share one demand.** `nMounts` is 2 when `bWeaponL == bWeaponR` and
1 when they differ, and the demand per weapon is 10, 3, 2, 10, 2 for weapons 25, 26, 27, 29, 30.
Weapon 29 is returned to the base in tens and drawn back in hundreds. The two mounts share one
ammo byte, so the right mount re-reads what the left mount just wrote.

**A shortfall takes the base column *after* the craft's own ammo was added to it.** With 3 at the
base and 7 aboard, the craft ends with 10, not 7. That is the second time this session a test
expectation of mine was wrong rather than the port.

### Two original bugs, reproduced and asserted

1. **The hangar marker goes into the wrong base's grid.** The three grid *reads* in the scan scale
   by `24 * bHomeBase`; the one *write* scales by `24 * bCraftType`. A type 1 craft landing at base
   3 writes its marker into base 1's facility grid. Proven from `MUL R2,R3,R8` at 0x6858C where R3
   is `Ship.bCraftType`.
2. **`Base_UpdateStoresNeeded` is called with the wrong base index.** 0x689E4 sign-extends R8, and
   R8 was last written at 0x68654 as `2 * ship index` from the stack slot holding the ship index.
   So the argument is `(i8)(2 * shipIdx)`: ship 1 updates base 2, ship 4 names base 8, past the
   eight base blocks. Hex-Rays renders it as `(char)nBaseIdx`, which is wrong.

### `Base_CalcStorageUsed @ 0x6555c` (done)

Sum of `bCargoSize * auPerBaseVal[nBase]` over a fixed set of object indices: 1..69, 105..111,
122..124 and 140. Its first guard compares the running pointer against the Map base, which is true
on every iteration, so it reduces to `index < 70`. This is one of the two budget inputs
`Base_UpdateStoresNeeded` uses internally; the other, `Base_CountItemQtyAcrossBases` (0x67d68), is
still unported, so the needed-flag pass this call performs is weaker than the original's. That is
the port's one listed deviation in this function.

Covered by `TestTransferTacticalLoot`, 28 checks.

## `Base_DemolishFacility @ 0x66b70` — ported

1,600 bytes. Despite the name it removes a **whole base**. find the base whose 64-byte
record at `0x186C40` has `nFacType` equal to the argument, drop that base's strateg entry, delete
the soldiers stationed there, remove every craft homed there, mark its map sites unassigned,
renumber home bases and soldier assignments, compact every per-base array, drop the count byte at
`0x159D80`, delete `apBases[newCount-1]`, and renumber `BaseRec.bSlot`. All five callees are
ported, so nothing else is missing.

Two findings worth having on record:

- **The argument is used with two meanings in one function.** At 0x66BB8 it is a facility type
  compared against `0x186C40[i].nFacType`; at 0x66BE4 it is a **site id** compared against
  `StrategItem.nSiteId`. If no strateg entry matches, the index equals the item count and
  `StrategList_RemoveAt` is called one past the end.
- **The research column stride is 288 bytes per base, not 144.** `MOV R3, #0x120` at 0x66E88.
  Hex-Rays renders 144 because it types the pointer as `u16 *`.

### The compaction, and the stride that looked wrong

The compaction shifts six per-base arrays in parallel. All strides are now confirmed from the
disassembly rather than the decompiler:

| array | per-base stride | where it comes from |
|---|---|---|
| `0x186C40` facility records | 64 | `ADD R6, R0, R4, LSL#6` |
| `apBases` records | 20, moved through the pointers | `memmove` of `0x14` at 0x66F44 |
| facility block (four grids) | 144 | 4 × 6 cells at 0x66F4C |
| `buildDays` | 36 | `MOV R3, #0x24` |
| research column at `0x16022C` | 288 | `MOV R3, #0x120` |
| product B at `0x15B79C` | 800 (200 u32) | `MOV R3, #0x320` |
| product C at `0x15D09C` | 400 (200 u16) | `MOV R3, #0x190` |
| manuf quantity column at `0x15E19C` | 400 (200 u16) | same register, different fixed offset |

The one that looked wrong was the u16 stride. Hex-Rays renders it `100 * v27`, which reads as 100
bytes against a 400-byte column — but the anchor is typed as a four-byte element, so 100 elements
**is** 400 bytes. `MOV R3, #0x190` settles it. Reading Hex-Rays' literal here would have put a
wrong layout into the port.

### A third original bug, reproduced

Product B and product C land on column `idx - 1`, which is the correct compaction. The manufacturing
quantity column at `0x15E19C` lands on column `idx`. So it is shifted **one base later** than every
other array: the removed base keeps its manufacturing values, and the base after it takes the one
after that. `TestDemolishBase` asserts both halves — 111 stays where it is, and 333 moves into
column 2.

A miss in the first search is also reproduced. The index lands on the base count, the compaction
guard fails, and the function still drops the count and deletes `apBases[count-1]` — so asking to
demolish a type no base has removes the **last** base.

Covered by `TestDemolishBase`, 25 checks.

Test trap found here: `Soldier_RemoveById`, `Ship_RemoveWithCargo` and `StrategList_RemoveAt` free
what they remove. A test that frees those objects again corrupts the heap and the binary dies
before printing its summary — which looks like a pass under ctest if the stale binary is used.

## `Tact_Shutdown @ 0x3d568` — fully unblocked, mapped in the IDB, not yet ported

3,788 bytes. Every callee is now ported: `Sound_Shutdown`, `KillTimers_BySpeed`,
`UiPanel_FreeLines`, `Map_FreeRmpNodes`, `PadImage_FreeData`, `Font_Free`, `UiControl_FreePads`,
`PtrArray_DeleteAll`, `Soldier_RemoveById`, `StrategList_RemoveAt`, `Ship_RemoveWithCargo`,
`Base_DemolishFacility` and `Base_TransferTacticalLoot`. What remains is the shape of the function
itself, and the decode is recorded in the IDB at 0x3D6E4, 0x3DD0C, 0x3E06C, 0x3E158 and 0x3E328.

Four things worth having written down:

- **Thirteen `pBank` pad families**, each freeing three adjacent pad pointers and zeroing the slot
  it came from. Group sizes and starting widget offsets: 19 at 0, 13 at 19, 12 at 32, 29 at 60,
  7 at 53, 9 at 44, 9 at 89, 3 at 98, 3 at 101, 1 at 104, 8 at 105, 1 at 128, 1 at 129. Three of
  them are written `for (n = 0; n == 0; ++n)`, so they run exactly once.
- **Which units are removed is a filter, not a sweep.** Civilians go when `Soldier + 960` is zero;
  every enemy goes with no test at all; XCOM units go when `nUnitType` (`Soldier + 160`) is 4..8 or
  `Soldier + 960` is zero. The bound is re-read each iteration because the removal changes it while
  the index keeps advancing — so the walk can skip entries.
- **The tactical sprite set is 32 `PadImage` slots** freed as 4 × 2 × 4 from `Map + 0x199C`, which
  is the same block `Save_ReadTGameData` rebuilds.
- **The tactical map free runs in tenths of a cell**: 10 × `nMapDimY` rows by 10 × `nMapDimX`
  columns, four layer pointers per cell at a 14,400-byte stride.

`LoadGame @ 0x44bbc` is blocked by `Tact_Shutdown`, `Geoscape_InitUI @ 0x857f4` and
`Tact_InitMission @ 0x3c550`. Of those three, `Tact_Shutdown` is now the cheapest.

## The tact subsystem blocker map

Measured by checking every callee of `Tact_InitMission @ 0x3c550` against `src/pufo`. It is the
deepest remaining blocker, and the order matters: four of the nine are leaves with nothing else
behind them.

| callee | bytes | status |
|---|---|---|
| `Map_BuildMissionTerrain @ 0x22e10` | 10,676 | unported, the largest single gap in the tact subsystem |
| `Tact_SpawnAliens @ 0x3a2ac` | 3,516 | unported |
| `Tact_DeployTeamAndHwps @ 0x39980` | 2,348 | unported |
| `Tact_SetupCraftCargo @ 0x38f00` | 2,116 | unported; needs the ammo-split table at `0x156CD0` and the per-object work triple at `0x157208`, neither owned by the port yet |

`Tact_SetupCraftCargo` is worth one note before anyone ports it. Its guard reads
`v21 < 372 || v21 > 420`, which looks like a byte-range test and is not: `v21` advances by 12 per
object, 372 is 12 × 31 and 420 is 12 × 35, so it skips **object indices 31 through 35** — the craft
store slots. The same guard appears twice. Reading it as a byte range would silently include the
craft stores in a tally the original excludes them from.
| `Tact_EquipTeamFromCraft @ 0x3be24` | 1,836 | unported |
| `Tact_SelectTeamUnits @ 0x39744` | 572 | unported; needs `Tact_SelectUnit` and the deploy-cell table at `byte_A66B8` |
| `Map_PatchCraftSprites @ 0x25ab4` | 952 | unported |
| `Tact_SelectUnit @ 0x2c6c4` | 148 | unported; its only unported dependency was `Unit_FormatDisplayName (0x476d4)`, now ported — so it is unblocked |
| `Unit_FormatDisplayName @ 0x476d4` | 528 | **ported** |

Already ported and callable from here: `Tact_LoadUiPads`, `Tact_InitUiControls`,
`Tact_BuildVisRayTable`, `Tact_LoadLoftData`, `MapView_FocusSelected`, `Map_RefreshCellMeta`,
`MapCell_SetBFlags29_4`, `Save_ReadObjBefore`, `Save_ReadSoldiers`, `StartTimers_BySpeed`,
`GameSpeed_Set`, `Inv_OpenScreen`, `Map_InitFacingScratch`, `Font_Init`, `UiPanel_WrapText`,
`Palette_Load`, `StatusMsg_Push`, `Unit_bIsOperative`, `MapCursor_SetPos`,
`Unit_ResetBattleState`, `Unit_PlaceOnMap`, `Unit_RecalcInventoryStat`.

Three facts worth keeping about `Unit_FormatDisplayName`, because they decide how it gets modelled:

- `szName` is at `Soldier + 0x0ba` and `szDisplayName` at `Soldier + 0x2bc`, and the original writes
  the display name as `szName[514]`. 0x0ba + 514 is 0x2bc, so the two renderings of the same store
  agree, and the third path - copying the global text at `0x10DA38` to `pUnit + 700` - lands on
  `szDisplayName` too.
- The unit-type filter is not a simple range, and reading it as "0..3 XCOM, everything else alien"
  is wrong. The three families are:

  | family | nUnitType |
  |---|---|
  | rank `": "` name | 0..3 |
  | plain name, and **empty** for any mode other than 0 or 2 | 4..8, 14..20, 22 |
  | name `" - "` rank | 9..13, 21, 23 and above |

  An earlier note here put 14..20 and 22 in the third family. They are not: the middle condition is
  `t <= 8 || (t > 13 && (t <= 20 || t == 22))`, and the test pins 14, 15, 21, 22 and 23 individually.
- The middle family's "empty" is not a guess. It copies from `0x10DA38`, the .bss zero page — the same
  buffer `UiControl_LoadPads` compares pad names against as the empty string. Copying it clears the
  display name.

Modes 3 and above are asymmetric, and that is reproduced rather than tidied: the first and third
families write nothing at all and leave `szDisplayName` untouched, while the middle family clears it.

---

## The isometric layer cluster behind `MapView_DrawIsoLayer @ 0x21e2c`

`MapView_DrawIsoLayer` is 1,252 bytes but it is not one unit of work: it calls 17 functions, and 11
of them were unported. It is recorded here as a cluster so the size of the remaining work is visible
rather than hidden behind one address.

It walks the map twice per layer. The first pass draws floors only; the second draws everything else.
For each tile it calls the same gate, and when the gate fails and the layer is 0 it draws a single
fallback glyph instead. That fallback is the only reason the two passes share a shape.

| callee | size | state |
|---|---|---|
| `MapView_bResolveIsoCell @ 0x1b0d4` | 428 | **ported** — the gate: tile to map coordinates, bounds test, binds pCell/pUnit/nPal/nObjParam |
| `MapView_DrawCellSelectMarker @ 0x1b280` | 476 | **ported** |
| `MapView_DrawCellSelectMarkerHi @ 0x1b584` | 568 | **ported** |
| `MapView_DrawUnitHeightGlyph @ 0x1c240` | 1,132 | **ported** |
| `MapView_DrawViewportGuide @ 0x1aadc` | 1,256 | **ported** |
| `TactHud_DrawSelectedUnitPanel @ 0x1d5ec` | 1,484 | **ported** |
| `MapView_DrawUnitFacingArrow @ 0x1b7bc` | 2,692 | not ported |
| `MapView_DrawUnit @ 0x1c958` | 3,220 | not ported |
| `TactUnit_DrawAnimGlyphs @ 0x1c6ac` | 168 | **ported** |
| `TactUnit_DrawFacingGlyph @ 0x1c754` | 136 | **ported** |
| `TactUnit_DrawHpGlyphs @ 0x1b4b0` | 212 | **ported** — the offset it reads is proven; its meaning is not (see below) |
| `Map_IsoOffsetA/B @ 0x26f88 / 0x26fa8` | — | already ported in `pufo_mapcell.h` |
| `MapView_DrawCellFloorGlyph`, `MapView_DrawCellOccupantGlyph`, `MapView_SetWallPalFromSlotA/B` | — | already ported in `pufo_isodraw.c` |
| `Unit_bIsOperative @ 0x49eb8`, `StatusMsg_DrawStack @ 0x1afc4` | — | already ported |

### The sprite group bases, which the pseudocode obscures

The renderer's glyph indices are all built from per-group bases kept inside the font block at
`0x158268`. `Font_LoadTactSprites @ 0x257c4` is the only writer, and the ARM states the rule exactly:

```
LDR  R1, =aFloorob
LDRH R0, [R4,#0x36]     ; fontSys.nTotalSprFrames
STRH R0, [R4,#0x22]     ; base for floorob = the count BEFORE the load
BL   Font_LoadSpr
```

Each base is the running frame count captured immediately **before** its group is loaded, so it is
that group's first index. The table, from the `STRH` displacements:

| offset | group | | offset | group |
|---|---|---|---|---|
| +0x00 | blanks | | +0x20 | handob |
| +0x02 | cursor | | +0x22 | floorob |
| +0x04 | mission terrain (from `nTotalMc1`) | | +0x24 | tanks |
| +0x06 | craft (from `nTotalMc1`) | | +0x26 | bigobs |
| +0x2A | bullets | | +0x2E | scang |
| +0x30 | smoke | | +0x32 | expl |

Hex-Rays renders these as `gap_8[n]` and `gap_32[n]` and its indices do not agree with the ARM
displacements; the `STRH` displacements are authoritative. The four the leaf helpers read were
renamed in the IDB to `nHandobSprBase`, `nFloorobSprBase`, `nTanksSprBase`, `nBigObsSprBase`,
`nBulletsSprBase`, `nScangSprBase`, `nSmokeSprBase` and `nExplSprBase`.

The animation kind table at `unk_AA018` is `00 08 0C 10 00 04 00 00`, indexed by
`MapCell.nFlags32 & 7`. Kind 4 is the only kind that draws twice, and its second glyph uses the entry
for kind 1, which is 8 — the byte Hex-Rays calls `byte_AA019`.

### The IsoDrawCtx fields the pseudocode cannot show

`MapView_bResolveIsoCell` is where the context's layout becomes visible, and the ARM displacements are
the only trustworthy source. Hex-Rays reaches two of these through `pIso[1].pCell`, which is how it
renders a plain dword as a struct pointer field:

| offset | field | |
|---|---|---|
| 0x00 | `pCell` | 0x38 `nLayer` |
| 0x04 | `nDrawX` | 0x54 `nObjParam` |
| 0x08 | `nDrawY` | 0x58 `pUnit` |
| 0x1C | `nIsoOffA` | 0x64 `bWallPalIdx` |
| 0x20 | `nIsoOffB` | 0x6C `nPal` |
| 0x24 | `bOob` (byte) | 0x138 `nMapY` |
| 0x28 | `nTileX` | 0x13C `nMapX` |
| 0x2C | `nTileY` | |

The rules it applies:

- `nMapY = nIsoOffA + nTileX + nTileY / 2`, `nMapX = nIsoOffB + nTileY / 2 - nTileX`, plus one when
  `nTileY` is odd. The divide is the compiler's signed divide-by-two (`MOVS` / `ADDMI` / `ASR`), which
  truncates toward zero, and the odd test is the `__rt_sdiv` remainder being non-zero — so a negative
  odd row takes the `+1` too.
- Out of bounds is any negative coordinate, `nMapY > 10 * nMapDimX - 1`, or `nMapX > 10 * nMapDimY - 1`.
  The factor of ten is because the dimensions are stored in 6-tile units.
- `nDrawX = 32 * nTileX - 16`, or `- 32` on an odd row; `nDrawY = 8 * (nTileY - 3 * nLayer - 4)`.
- It returns false when out of bounds **or when `nDrawY + 40 <= 0`**. That is a 40-pixel lookahead, not
  a screen test: a cell whose top is 40 or more pixels below the draw origin is skipped, and one that is
  merely off the bottom edge is not.
- The cell is `apCells[60 * (60 * nLayer + nMapX) + nMapY]`. The ARM multiplies by `0x3C` twice, which
  is both the layer stride in cells and `sizeof(MapCell)` on the original — the compiler folded the two
  into one constant.
- `nObjParam` is the ceiling sprite's signed `iCeilParam` at sprite + 0x24, or 0 with no ceiling sprite.
  `nPal` is 255 unless the cell's `bFlags29` bit 4 is set, in which case it is 0 for animation kind 4
  and the cell's `bUnk56` otherwise.

### The two select markers, and the mode byte behind them

Both markers pick a pair of glyph offsets from `Map.gap_2[1]` — the port's `UnitRoster.bActionLatch` —
and draw them from the **cursor** sprite group (`fontSys + 0x02`). They differ in three ways that are
easy to get wrong:

- the gates differ. The low marker fires on `bFlags57[0]` bit 3 **or** `bFlags29` bit 6; the high marker
  only on bit 3.
- the second glyph is offset differently. The low marker draws B as an **absolute** index — its mode-3
  value is already `multi-target slot + nGlyphBase + 49`. The high marker adds the cursor base to B.
- the mode tables are not the same. Mode 2 uses `phase >> 1` in both, but mode 0 and 1 use `phase >> 2`
  only in the high marker, and the low marker's mode 0/1 path is the occupant test.

A miss in the multi-target scan is not a miss: the loop keeps the index, so a cell that is not
multi-targeted still gets a glyph, at index 8.

### `TactUnit_DrawHpGlyphs`, and one offset whose meaning is still unknown

Two glyphs, and the second one's source is easy to misread. The ARM is decisive:

```
LDR  R3, [R4]              ; pIso->pCell
LDR  R3, [R3,#0x34]        ; pCell->pItemList      -- the HEAD, reloaded
LDR  R3, [R3]              ; pItem->pObj
LDRH R3, [R3,#0x9E]        ; ObjectRec + 0x9E
```

- Rule 1 walks the item list while the item's object has `bTwoHanded` (`ObjectRec + 0x28`) equal to 2.
  Reaching any other value draws one glyph at the bullets base + 7. Running off the end of the list
  skips that glyph — but rule 2 still runs.
- Rule 2 reads the **head** item's object at `+0x9E`, which is `aItemQty[50]`, and if it is non-zero
  draws one glyph at the floorob base + (value − 1). It is not the item the walk stopped at. The test
  gives the head and the found item different values and asserts the wrong reading draws nothing.
- Both glyphs draw at `(nDrawX, nObjParam + nDrawY)` with the cell palette, not at `nDrawY` alone.

What is **not** known, stated rather than papered over: why column 50. `aItemQty` is a 52-entry u16
array at `ObjectRec + 0x3a`, and the ships chunk writes it indexed by ship slot; no writer of column 50
was found in the binary. The offset is proven from the ARM. Its meaning is an open question, and the port
reproduces the offset without inventing a name for it.

### `MapView_DrawUnitHeightGlyph @ 0x1c240` — ported, and the trig turns out to be a rotation

It draws the firing-arc height glyph for the cell that **is** the current fire target.

The gate, six conditions, all required:

| test | where |
|---|---|
| `Map + 0x52` byte non-zero | the port's `bFireStatus2` |
| `Map + 0x59` byte non-zero | now named `bFireStatus9` |
| `nMapX == nFireTgtY` | `0x14044` |
| `nMapY == nFireTgtX` | `0x14040` — **X and Y are swapped** against the iso fields |
| `nLayer == nFireTgtZ` | `0x14048` |
| `nHeightGlyphId != 0` | `0x14000` |

The arithmetic, with `A = nHeightSrcX % 16`, `B = nHeightSrcY % 16`, `C = nHeightSrcZ % 12`. Written the
way the original writes it, folded constants and all:

```
nHeightOff = ((A*sin*scale) + 2.0) - (B*sin*scale) - 2.0
nHeightAux = ((B*cos) + (A*cos)) * scale + 40.0 - 2.0 - (C*2.0) - 20.0 - 8.0

index  = 10 * LOWORD(nHeightGlyphId) + LOWORD(nFireTickProgress) + nSmokeSprBase + 16, palette 1
position = (nHeightOff + nDrawX, nDrawY + nHeightAux + 15)
```

**The two constants collapse the whole trigonometry to integers.** With `angle = atan2(32, 16)` and
`scale = sqrt(320)/16`:

```
sin(angle) * scale = 32/sqrt(1280) * sqrt(320)/16 = 2 * sqrt(1/4) = 1.0
cos(angle) * scale = 16/sqrt(1280) * sqrt(320)/16     = sqrt(1/4)   = 0.5
```

so the same arithmetic is

```
nHeightOff = A - B
nHeightAux = (A + B)/2 - 2*C + 10
```

That is the isometric rotation of a position pair: one axis runs along the tile diagonal, the other is
half the sum, and the third term is height. It is also the strongest evidence for what the three
operands are — **X, Y and Z**, not three heights — which is why the globals were renamed from
`nAimHeightRawA` / `nAimHeightRawB` / `unk_127A20` to `nHeightSrcX` / `nHeightSrcY` / `nHeightSrcZ`.
The producer of the triple is still not identified, and the names say "source", not where it comes from.

An earlier note here gave `nHeightAux` as `(A + B) * cos * scale * scale - 2*C - 1`. That was wrong in
two ways: `scale` is applied once, not twice, and the four folded immediates net to `+10`, not `-1`.
The constants are `1076887552` = 2.0, `1078198272` = 40.0, `1077149696` = 20.0, `1075838976` = 8.0, and
`40 - 2 - 20 - 8 = 10`.

The port reproduces the original operation order rather than the simplified pair. `sin * scale` is
mathematically 1.0 but is the product of two rounded transcendental functions, so the result lands on an
integer only approximately — and the value is truncated to int. A different association could move it
across the boundary.

Why it was deferred, and why that is resolved: `angle` (`0x1571F8`) and `scale` (`0x156AA8`) are .bss
doubles set elsewhere, and the port had no owner for either. Porting then would have meant inventing two
globals with no proven writer.

Both writers turned out to be tiny, and both are now ported:

| original | size | what it stores |
|---|---|---|
| `Math_StoreAtan2Const @ 0x370e8` | 56 | `atan2(32.0, 16.0)` = 1.1071487177940904 rad, about 63.43 degrees |
| `Math_StoreSqrtConst @ 0x37098` | 76 | `sqrt(320.0) * 2^-4` = `sqrt(320)/16` = `sqrt(5)/2` = 1.118033988749895 |

The angle's tangent is exactly **2**, which is the 32 by 16 isometric tile ratio — the same ratio the
resolve step uses in `nDrawX = 32 * nTileX` and `nDrawY = 8 * (...)`. The scale constant's multiplier is
the double `0x3FB0000000000000`, which is `0.0625` exactly; Hex-Rays renders it as the raw word
`1068498944`, which reads like an arbitrary integer until you unpack it.

Each global has exactly two readers in the whole binary: `MapView_DrawUnitFacingArrow (0x1b7bc, not
ported)` and `MapView_DrawUnitHeightGlyph @ 0x1c240`. They are iso-draw constants, not general math
state, and the port names them `g_dblIsoAngle` and `g_dblIsoScale` for that reason.

One trap avoided on the way. The ARM reads:

```
MOVS R3, R0
ADDMI R3, R3, #0xF
MOV  R3, R3, ASR #4      ; looks like a divide by 16
SUB  R0, R0, R3, LSL #4  ; ...but this subtracts the quotient back: it is a MODULO
```

The division is intermediate. Hex-Rays rendered `% 16` correctly, and "correcting" it to `/ 16` would
have introduced an error rather than removed one.

### `MapView_DrawViewportGuide @ 0x1aadc` — the tactical viewport frame

Fourteen keyed blits of one source tile, `(172, 184)`. Thirteen form the frame at key 150; the last one
is an indicator at key 128.

```
v2 = 303 when the deploy panel is open, else 320 - pHudPad->uHeight
v3 = v2 / 3,  v4 = v2 - v3
```

The gate is four conditions: `bGameTickHold` clear, `anActionPhaseSave0[94]` non-zero,
`bPlayerInputEnabled` non-zero, and the cursor not busy.

Two details worth recording:

- the frame is **not symmetric**: the top horizontal run is 210 wide, the bottom one is 211.
- the indicator's priority order is **bottom, top, left, right** — not the order the four bytes are
  stored in. And the top variant's vertical run is `v3 - 14` while the bottom variant's is `v3 - 15`.

One thing the test had to learn the hard way: `SoftSurf_BlitSrcRect` **blends** source and destination
by the colour key rather than copying. A pixel covered by one segment reads 813, by two reads 1138. So
the assertable fact is covered-versus-not-covered, not a pixel value. Asserting an exact colour here
would have been asserting the blend arithmetic, which is a different function's business.

### `TactHud_DrawSelectedUnitPanel @ 0x1d5ec` — ported, and it names the 19 HUD controls

Every callee was already ported, so this was mechanical. Two readings in the pseudocode are misleading
and had to be resolved against the struct layout.

**`pObj->aItemQty[0x34]` is not an array element.** `aItemQty` is 52 u16 entries at `ObjectRec + 0x3a`,
so index 52 is one past the end — and `0x3a + 104 = 0xA2`, which is `uIconId`. The weapon icon glyph is
therefore `uIconId + nBigObsSprBase − 1`: **weapon icons come from the bigobs sprite group**. Reading it
as a quantity would have been wrong.

**`*(u8*)(*pHand + 174) != 255` is `bAmmoCap`.** `ObjectRec + 0xAE`, where 255 means the weapon never
runs out of ammo. The value drawn next to it is `TactItem + 6`, which is `nAmmo`. So the panel shows a
clip count only for weapons that have one.

The rest of the shape:

- it **re-bases the panel and 19 bank controls first**: `v1 = 303` when the deploy panel is open, else
  `320 − pHudPad->uHeight`, stored into `pHudPad->nOriginY`, then
  `for (i = 0; i < 608; i += 32) pBank[i].y = off_A7ED8[i + 2] + v1`. 608 / 32 = 19, and those are the
  same 19 controls it then blits.

  That table turned out to be the tactical HUD's control layout, with **names stored in the binary**.
  `off_A7ED8` @ `0xA7ED8` is 19 `UiControl` records of 32 bytes, renamed in the IDB to
  `aHudTactControls`:

  | control | x | y |
  |---|---|---|
  | `nextu` | 8 | 20 |
  | `uplev` | 46 | 20 |
  | `cuplev` | 84 | 20 |
  | `map` | 121 | 20 |
  | `invent` | 159 | 20 |
  | `endturn` | 197 | 20 |
  | `dnlev` | 46 | 38 |
  | `cdnlev` | 84 | 38 |
  | `sit` | 121 | 38 |
  | `center` | 159 | 38 |
  | `alwaysrs` | 50 | 60 |
  | `singles` | 69 | 60 |
  | `aims` | 50 | 73 |
  | `autos` | 69 | 73 |
  | `mainmenu` | 0 | 3 |
  | `pmenu` | 206 | 3 |
  | unnamed | 92 | 60 |
  | unnamed | 9 | 39 |
  | unnamed | 198 | 39 |

  The panel rewrites only the y field. The x values are the controls' fixed columns and nothing here
  touches them, which is why the layout survives a panel resize.
- weapon centring is `nOriginX + 19*(2 − nFootW)/2 + 9` on the left and `+ 198` on the right, with
  `y = nOriginY + 17*(3 − nFootH)/2 + 41` — a 2×3 footprint of 19×17 pixel cells.
- the ammo number is shown when `(bTwoHanded == 0 && nCategory < 3) || Unit_bIsLarge(unit)`.
- four decimals of width 3 with leading zeros: TU (forced to 0 when `unk_157198->bUnk70114` is set),
  stun/armor at (109,60) and (109,75), HP and morale at (160,60) and (160,75).
- a 12-segment animation bar at `(nOriginX + 85, nOriginY + 13)`, each segment 4×2 on a 6-pixel pitch,
  whose profile depends on `apUiSlotsA[26]`: mode 0 is a clamped ramp, mode 1 a triangle over
  `slot25 % 32`, mode 2 a peaked profile with key `120 − 10·|v21|` where `v21` counts up from `−v19`.
- **when the counter reaches its end the next animation mode is chosen at random**: `slot25 = 0` and
  `slot26 = Game_RandU16(3)`. That is why no test may assert on a specific mode following an animation.

Both markers have the same latent bug, recorded in the ledger: an action-latch value above 4 that is not
handled by the switch jumps straight to the draw with `nGlyphA` never assigned, so the marker is drawn
with whatever glyph index the previously drawn cell left in the shared context. The port reproduces it.
