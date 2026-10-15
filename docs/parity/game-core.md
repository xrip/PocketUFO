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
  0     transparency: advance x, leave the pixel alone (ARM 0xa4288 gates the write on `*v5 != 0`)
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
| Index 0 is **transparency** (ARM 0xa4288 `*v5 != 0` gates the write) | asserted |
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
every one satisfies `size == 3 + 4 * nRows * nCols * levels`.

```
u8  nRows      template height in map cells   (header byte 0)
u8  nCols      template width  in map cells   (header byte 1)
u8  nLevels    number of z levels
u8  aCells[4 * nRows * nCols * nLevels]
```

Cell indexing, from `CDialogTemplate_GetCellByte` @ `0x54c80` (the trailing `+3`
skips the header):

```
aCells[4 * (nRows*nCols*z + nCols*y + x) + nSlot + 3]
```

Each cell carries four sprite slots, read by `Map_ApplyTemplateToCells` as floor,
wall A, wall B, ceiling. A slot value of **0 or 1** means "no sprite" (the
original tests `<= 1`); anything above is a real index into the font sprite base.

### Header byte 0 is the Y extent, not the X extent

The size formula is symmetric, so it cannot tell the two apart, and an earlier
version of this section -- and the port -- had it backwards. Three independent
reads in the original agree:

- `Map_ApplyTemplateToCells @ 0x54cf4` builds its destination as
  `10 * (6 * (10 * nOffY + row) + nOffX) + col`, where `row` loops to header byte 0 and `col` loops
  to header byte 1. The flat index is that of `(x, y) = (10 * nOffX + col, 10 * nOffY + row)`, so byte
  0 counts rows and byte 1 counts columns. The `6` and the `10` multiply to the 60-cell row stride.
- `CDialogTemplate_GetCellByte @ 0x54c80` uses header byte 1 as the row stride, which only makes
  sense if byte 1 is the column count.
- Every `Map_LoadRmp` call in `Map_BuildMissionTerrain (0x22e10)` passes `nSizeX = byte1 / 10` and
  `nSizeY = byte0 / 10`.

**What that broke in the port.** `MapTemplate_ApplyToCells` looped `nX < nDimX` and `nY < nDimY` and
passed those straight to `MapTemplate_GetCellByte`, whose stride is `nDimY`. For a non-square template
that reads past the end of every row and lands the template transposed on the map. `AVENGER.MA1` is
20 rows by 10 columns, so it stamped a 20-wide by 10-tall area instead of 10 wide by 20 tall. The
offsets were also treated as cell coordinates when the original scales them by `PUFO_CELLS_PER_TILE`.

Fixed: the fields are now `nRows` / `nCols` so the mistake cannot recur, the traversal is
`nRows` outer / `nCols` inner / `nLevels` innermost as the ARM nests them, and `nOffX` / `nOffY` are
tile coordinates. `test_template.c` now asserts the direction directly -- the far corner must be
`(x = cols-1, y = rows-1)` and the transposed corner must be untouched -- plus that a tile offset
lands ten cells in, not one.

Samples verified in the test: `AVENGER.MA1` 2403 bytes / header `20 rows x 10 cols x 3 levels`,
`CULTA0.MA1` 403 bytes / header `10 x 10 x 1`. `test_template` asserts all 600 cells of `AVENGER` are
visited with `SetTileSprites` applied, and that the last cell ends exactly at the blob end.

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
| large | 0 | 0 | 0 | 0 | 1 | 1 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

| type | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 |
|---|---|---|---|---|---|---|---|---|
| flag0 | 0 | 0 | 1 | 0 | 0 | 0 | 0 | 0 |
| large | 0 | 0 | 1 | 0 | 1 | 1 | 0 | 0 |

`g_aUnitTypeFlag0` is `byte[4i]` and `g_aUnitTypeIsLarge` is `byte[4i+1]`. That second identity is not an
assumption: `Unit_bIsLarge @ 0x49ecc` is literally `byte_A7B80[4 * pUnit->nUnitType + 1] == 1`.

An earlier version of this table recorded `large` as 1 for types 11 and 13. The image says 0 for both, and
the real extra slots are 18, 20 and 21. The rows above were re-read row by row rather than from one long
dump, because transcribing a 96-byte run is where the mistake was made.

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

### The six geoscape speed buttons (done)

`GameTime_Set5Sec @ 0x84f00`, `GameTime_Set1Min @ 0x84f64`, `GameTime_Set5Min @ 0x84fc8`,
`GameTime_SetSpeedIdx3 @ 0x8502c`, `GameTime_SetSpeedIdx4 @ 0x85094`, `GameTime_SetSpeedIdx5 @
0x850fc` — ported in `pufo_geoscape.c` as six thin wrappers over one `GameTime_SetSpeedIdx`
(13 new checks in `tests/test_geoscape.c`). Each plays sample 17, stores the tick interval into
`nGeoscapeTickSeconds` (`unk_187070`, renamed; the port's `GeoscapeWorld.nTickSeconds`, read by
`StrategList_UpdateMotion`), and sets the `nState` of the six nav speed controls — the `UiControl`
pointers `Geoscape_InitUI` keeps at 0x186500..0x186514, now typed as `apGeoscapeSpeedCtl[6]` — so
exactly the button's own control is 1 and the other five are 0. Intervals: 5, 60, 300, 1380, 2460,
3600 seconds. `SetSpeedIdx5` is the only one that ever lights control 5. Each original returns the
sixth control pointer (register residue); no caller tests it, so the port returns void.

### `Base_CreateNamedEntry @ 0x66930` — creating a new base entry (done)

Ported in `pufo_geoscape.c` behind `BaseCreateEnv`, 33 new checks in `tests/test_geoscape.c`. Two
callers: `Game_StartNew @ 0x94964` with (-6000, -6000, 0, "new") for the starting base, and
`BaseFacScreen_DrawPlacementOverlay @ 0x59AFC` with the pending placement coordinates and
`nFacType`.

It chains two already-ported helpers and then fills three records. `BaseRec_Create` puts a fresh
20-byte `BaseRec` at `apBases[nBaseCount]` (the count is raised only at the end); `StrategList_AddItem`
appends a type-0 site entry at (x, y) with `nId = nNextBaseSiteId` (`dword_186558`, renamed). The
strateg entry at index `nBaseCount` gets `bRandA = nKind - 0x80` — the ARM is `SUB R11, R10, #0x80`
at 0x669B4; Hex-Rays renders it as `+ 0x80`. The `aBaseFacilities` record (0x186C40) gets the name
(copied to its NUL with no bound — ORIGINAL BUG 0x669D0, the field is 20 bytes; the port truncates),
`bType = nKind - 0x80`, three zeroed scratch dwords, and `nFacType = ` the site id. The `BaseRec`
gets `bUnk17 = nKind`, `nUnk8/nUnk12 = x/y`, and `bUnk16 = abBaseKindTable[nKind] - 1` (the runtime
byte table at 0x1864DD, 15 entries, all zero in the image).

Then the economy clear for this base: the research column's first `Map_nObjectCount` u16 (149
iterations over a 144-entry column — five spill into the next base, reproduced), and entries `[i]`
and `[i + 2176]` of the u16 product column (0x15D09C + 400n) and `[i]` and `[i + 1600]` of the u32
column (0x159E9C + 800n). Finally `++nNextBaseSiteId` and `++base count`; the return is the
constant 1560 (the 0x618 count-store offset — register residue, reproduced).

### The craft-select leaves (done)

`Smap_DismissSelected @ 0x84428` (92 B), `Smap_InitCraftSelectState @ 0x61b94` (248 B) and
`EndScreen_Init @ 0x388b0` (328 B) — the last unported callees of `Smap_BeginCraftSelect @ 0x838dc`,
which is now fully unblocked. 42 new checks across `tests/test_geoscape.c` and `tests/test_titles.c`.

**`Smap_DismissSelected`** (ported in `pufo_strateg.c` beside `Smap_ClearSelection`) is that function
plus one step: the selected craft — its index lives in **colour slot 0 of line 0** of the root line
list, the same slot `Smap_BeginCraftSelect` reads at 0x839CC — is sent home via `Ship_RetargetTravel`
(0x84448) before the line is dropped (0x84454), the game unpauses only if the removal left the list
empty (0x84460), and `bMapSelectionFlag` (byte_159E84, renamed) is cleared. Deviation: the original
hands `Ship_RetargetTravel` the root panel pointer 0x158890, which that function never reads; the
port passes the removal env it actually needs.

**`Smap_InitCraftSelectState`** (ported in `pufo_geoscape.c` beside `BaseFac_SetCellBuildState`)
builds the 36-byte tactical tile-type grid `abTactTileTypes @ 0x159D5C` (read by
`Map_BuildMissionTerrain` at 0x22EA4 and `Tact_DeployTeamAndHwps` at 0x3A06C) for an assault on one
base. The grid starts as 36 copies of 20 ("empty"). A cell with zero `buildDays` (lane
`grid + 36*base`) takes `aCraftPlacementCodeByFacType[facType]` (byte_A6568, renamed) where facType
is the fac-lane byte at lane offset +108 — the same `6*(row+18)` offset `BaseFac_SetCellBuildState`
returns as residue; facType 0xFF and cells under construction contribute nothing, and a cell already
stamped by a hangar is skipped (the `== 20` gate). The hangar (11) stamps 16/17/18/19 at byte offsets
+0/+1/+6/+7 from its anchor — the ARM's own displacements at 0x61C3C..0x61C60, the same arithmetic
`BaseFac_SetCellBuildState` uses for its tileA quadrants, so at column 5 the +1 wraps to the next
row. Latent original bug 0x61C34 (ledger): an anchor at row 5 writes past the 36-byte grid into
0x159D80, the base count byte; the port refuses the past-the-grid stores. Deviation: the original
reaches the tile grid at `grid + 1524` (the two blocks are adjacent globals); the port takes them as
separate pointers and returns void where the original returns the grid base.

**`EndScreen_Init`** (ported in `pufo_titles.c`) sets up the credits screen for an ending. The two
endings differ in four values only: rows 10/16 into `nTitlesMaxRows`, the row table
`aGoodEndRowRecs`/`aBadEndRowRecs` (the image blobs at 0xA6458/0xA6480 — text records 0x56B..0x574
and 0x575..0x584, copied 40/64 bytes into `aTitlesRowRecs @ 0xB35D8`), `goodend.pal`/`badend.pal`
into palette slot 0x15, and music track 4/3. The font step is the easy one to misread:
`fontSys_nGlyphBase` (0x158290 = fontSys + 0x28, renamed) is set to the running frame count BEFORE
`Font_LoadSpr("font")`, so the glyph base becomes the first frame index of the font group. Then the
"z" samples, the pad glyph (`goodend.pad`/`badend.pad` allocated to the archive-reported size into
`pTitlesBackdrop`), both volume applications, `StartTimers_A`, screen id 18, scroll 0. Deviation: the
original's `operator new` is unchecked; the port skips the read when the allocation failed, and
passes the option block's volumes explicitly where the original reads them from globals.

Named this step: `nNextBaseSiteId` (0x186558), `abBaseKindTable` (0x1864DD), and `nSmapParam3` →
`nStrategSeedCounter` (0x18640C, the per-item seed `StrategItem_ctor` writes).

### `Smap_BeginCraftSelect (0x838dc, 2424 B)` + `Smap_BeginCraftSelectBtn (0x84634, 20 B)` (done)

The whole craft-select decision tree, ported in `pufo_strateg.c` behind a `SmapCraftSelectEnv`
(Map-block pointers, geoscape state, the `SmapView` + overlay tiles + alt map, the ship-removal and
base-demolish envs, and three injected hooks standing in for `GeoscapeUi_FreeAll`,
`Tact_InitMission` and `EndScreen_Init`). 46 new checks in `tests/test_geoscape.c`.
`Smap_BeginCraftSelectBtn` is the assault-confirm nav button: set `bCraftSelectAssaultFlag`,
tail-call.

The order Hex-Rays garbled but ARM pins:

1. **Visibility pass first** (0x8391C..0x8398C), before any mode gate: every craft with
   `bAtBase != 0 || bWeaponsHot == 0` is parked (`bWeaponsHot = 0`, `bAtBase = 1`) and its strateg
   entry hidden (`bVisible = 0`).
2. **Selection pass** (mode != 1, 0x839A8..0x83C64): `unk_128AD6 = 1`; the ship index is line 0
   colour 0 (→ `0x187054`, `0x159E74`, `Map_nCraftIndex`), the strateg index colour 1 (→ `0x186BE4`);
   the overlay tile under the item below 0x32 clears `unk_128AD6`; the item's race picks one of five
   sprite sets (`g_aRaceSpriteSet`: {9,18,23,23}, {10,15,19,23}, {11,20,23,23}, {12,14,23,22},
   {13,21,23,23}).
3. **Line flag branch** (0x83C64..0x83F10): flag 21 → urban type 5 mode 3 + `StrategList_RemoveAt`
   of the selected item; flag 22 → alien base type 4 mode 2; flag 23 → crash landing, mission type
   from the alt-map tile (240→desert 6, 241→jungle 0, 242→polar 8, 243→forest 2, 244→culta 1 —
   cross-checked against `g_aTerrainModuleCount`'s order). Then the craft-class tier jumptable
   (0x83D2C): budgets 50/200/250/500/500/1200/2200/3000, variants 0,1,1,2,2,3,3,4; the damage band
   is `100*(budget-nHp)/budget` (signed `rt_sdiv`), clamped to 70 then divided by 3 (magic
   0x55555555>>32); `nHp == -1` → 0. `nMapLayers = bCraftType + 1`.
4. **Xcom base assault** (mode == 1, 0x84018..0x84240): sprite set by `nAssaultBaseRace`, type 3,
   `Smap_InitCraftSelectState(nCraftHomeBase)`, `nSelectedBaseIdx = base`. Soldiers assigned →
   straight to the tail (mode stays 1). Otherwise: message 17 append (flag 0x11, a3 = base idx),
   radar pulse 250, funding `-= 800 + 400*(crafts with bHomeBase == base, sign-extended i8 compare)
   + 30*Base_CalcOccupiedSpace`, `0x15961C = 1`, `unk_187084 = 0` (a write-only byte),
   `Base_DemolishFacility(apBases[base]->bUnk4)`; base count 0 → `FreeAll(1)` + `EndScreen_Init(0)`
   + tick-hold + battle-ended + return 1, else return the count.
5. **Tail** (LABEL_10, 0x83A0C): assault flag → Mars-special type 9 + sprite {13,21,23,23} + mode 0
   + spawn-alt 1; mode != 1 → `Smap_DismissSelected` — the Xcom launch keeps mode 1 so it is
   **skipped** and the line survives, game stays paused; then `bMapSelectionFlag = 0`, `FreeAll(0)`,
   return `Tact_InitMission(0)`.

Two behaviors worth knowing. The retarget inside `DismissSelected` runs
`Ship_RetargetTravel → StrategEntry_InitTravel`, whose `bB` argument is 1, so it **re-raises**
`bVisible` over the visibility clear — the entry is visible again after the launch. And the funding
loop compares `bHomeBase` sign-extended: a base index of 0 matches byte 0, so a craft homed at base
0 is charged 400.

Deviations (recorded in the header): the default craft-class arm leaves the budget 0 and the
original divides by it — the port skips the band; the `apBases[base]->bUnk4` dereference is
unchecked in the original. IDB: call edges from 0x838dc verified against `Smap_InitCraftSelectState`
(0x84124), `Base_CountSoldiersAssigned` (0x84134), `Base_CalcOccupiedSpace` (0x841c0),
`Base_DemolishFacility` (0x84208) and `UiPanel_AppendLineWithSound` (0x84158, port name
`UiLineList_AppendWithSound` recorded in renames.tsv).

### The research screen chain, first pieces (done)

`Research_TopicNeedsItem @ 0x72c8c` (136 B) and `ResearchScreen_RefreshPanels @ 0x73008` (568 B),
ported in `pufo_baseui.c` with 24 checks in `tests/test_baseui.c`. They open the **in-memory**
research topic table — 144 records of 34 bytes at `0x15EF08` (`MUL R3, R0, #34` at 0x72CC8), which
is NOT the on-disk field order `pufo_save.h` writes. The fields the original itself names:

| offset | field | evidence |
|---|---|---|
| +4 | u16, the weapon fire gate reads it | `pufo_combat.h` FireGate (`aFacTypeTable[4 + 34*topic]`) |
| +8 | u16, progress total (divisor) | 0x73174 `LDRH R0,[R9,#8]` |
| +10 | u16, progress value (numerator) | 0x73160 `LDRH R3,[R9,#0xA]` |
| +12 | byte, 1 = the topic is on the screen list | 0x73034, 0x730B8 |
| +13 | byte, the weapon menu gate reads it | `pufo_tactui.h` (0x17078..0x17094) |
| +14 | u8, the item the topic needs | 0x72CE4 `LDRB R3,[R3,#0xE]` |

`Research_TopicNeedsItem` answers one question — does the topic's item exist at the base being
shown — and its gate is **two ARM bands**, not the boolean Hex-Rays reconstructed: 0x72C9C branches
on `<= 54` straight into the 117/143 test, so topics 55..65 and 118..142 need an item (135 and 140
excluded by name at 0x72C8C/0x72C94) and everything else does not. The lookup is
`apObjects[item]->auPerBaseVal[nSelectedBaseIdx] > 0`, unguarded on both the index and the pointer.

`ResearchScreen_RefreshPanels` rebuilds the three-panel trio the paging handlers page
(`unk_160C68` / `unk_15EEFC` / `unk_15EF00`): pass 1 counts topics 1..count-1 with `bActive == 1`
(topic 0 is never walked — the walk starts at 0x15EF2A) and writes that count into all three
panels' `nLineCount`; pass 2 writes the quantity column entry and the progress percentage into the
two text panels. The quantity entry address is `0x15EF08 + 4900 + 288*base + 2*topic` — the ARM
composes it as `MUL base,#0x90`, `ADD topic`, `LSL #1` (0x730C0..0x730E4), so the entry index is
the topic number itself; the `+4` Hex-Rays showed is the 0xBE4 `apLines` offset leaking through
register R4, not a column header. Progress is `100*wProgress/wTotal` (`rt_sdiv` at 0x73184) with
`%` appended; a zero entry or zero progress gets the empty string at 0x10DA38. Ledger row: the
original tests only the numerator, so `wProgress != 0 && wTotal == 0` divides by zero — the port
skips the store (same policy as the craft-class band).

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
against `Craft_GetTypeCargoCap`. Its own port was blocked on modelling the hangar screen's state
block (`0x161A40`, `0x161AC8`, `0x161AD0`, `0x161EE0`, `0x161EEC`, `0x161EF4`) -- that block is now
modelled as five `UiPanel *` slots plus `anSlotCarryCount`, and the function is ported; see "The hangar
stores list: take a row, put one back".

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
| `Map_BuildMissionTerrain @ 0x22e10` | 10,676 | **ported** — `src/pufo/game/pufo_terrain.c` |
| `Tact_SpawnAliens @ 0x3a2ac` | 3,516 | **ported** |
| `Tact_DeployTeamAndHwps` (0x39980) | 2,348 | ported |
| `Tact_SetupCraftCargo @ 0x38f00` | 2,116 | **ported** |
| `Tact_EquipTeamFromCraft @ 0x3be24` | 1,836 | **ported** (6daa141) |
| `Tact_SelectTeamUnits @ 0x39744` | 572 | **ported** (2883497) |
| `Map_PatchCraftSprites @ 0x25ab4` | 952 | **ported** (5fdd234) |
| `Tact_SelectUnit @ 0x2c6c4` | 148 | **ported** (32057bf, corrected in e34ce4a) |
| `Unit_FormatDisplayName @ 0x476d4` | 528 | **ported** |

`Tact_SetupCraftCargo` is worth one note. Its guard reads `v21 < 372 || v21 > 420`, which looks like
a byte-range test and is not: `v21` advances by 12 per object, 372 is 12 × 31 and 420 is 12 × 35, so
it skips **object indices 31 through 35** — the craft store slots. The same guard appears twice.
Reading it as a byte range would silently include the craft stores in a tally the original excludes
them from.

**What the two scratch globals turned out to be.** `0x156CD0` and `0x157208` were listed as blockers
because the port owned no model for them. Cross-referencing settles it: each has xrefs from
`Tact_SetupCraftCargo` and from nothing else, so they are private scratch, not shared state. They are
still not per-call state. The function zeroes one `0x156CD0` entry per object it reaches — at
`0x392AC`, after the `bAmmoCap == 255` test and before the score and `bTwoHanded` tests — and it
writes the `0x157208` triple only for objects that get that far. An object it leaves early keeps
whatever an earlier call left, and the item-build loop reads both back. The port takes them as
caller-owned arrays in `TactCargoEnv`, which is what `.bss` residency means, and
`tests/test_tactctl.c` (`TestCargoScratchPersists`) calls the function twice to pin it.

**The `2*(base+21)` rendering is a trap.** At `0x390C8` Hex-Rays writes `*(obj + 2*(base + 21))`,
which looks like a second array past `auPerBaseVal`. `2*(base+21)` is `42 + 2*base`, and
`auPerBaseVal` starts at 42, so it is the same u16 the previous instruction read. The pair is "move
the base column into `uRecovScore`, then clear the column".

**The cargo-to-store mapping is corroborated, not guessed.** `Ship_ReturnToBase` (0x686F0) hands the
same four `Ship.anCargoQty` columns to objects 56, 57, 105 and 58 and divides column 2 by 10, exactly
as `Tact_SetupCraftCargo` does at 0x39600. Two independent ARM sites, same mapping, same tens rule.
The ten itself is not explained by anything in the binary, so it stays a literal.

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
of them were unported when this cluster was first written. All 17 are ported now, and the
orchestrator itself is ported as `MapView_DrawIsoLayer(IsoDrawCtx *, const IsoLayerEnv *)` — see the
section at the end of this cluster.

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
| `MapView_DrawUnitFacingArrow @ 0x1b7bc` | 2,692 | **ported** in `src/pufo/game/pufo_isodraw.c`, behind `IsoArrowGate` |
| `MapView_DrawUnit @ 0x1c958` | 3,220 | **ported** in `src/pufo/game/pufo_isodraw.c`, behind `IsoUnitDrawGate` |
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

Each global has exactly two readers in the whole binary: `MapView_DrawUnitFacingArrow @ 0x1b7bc` and
`MapView_DrawUnitHeightGlyph @ 0x1c240`. They are iso-draw constants, not general math state, and the
port names them `g_dblIsoAngle` and `g_dblIsoScale` for that reason.

One trap avoided on the way. The ARM reads:

```
MOVS R3, R0
ADDMI R3, R3, #0xF
MOV  R3, R3, ASR #4      ; looks like a divide by 16
SUB  R0, R0, R3, LSL #4  ; ...but this subtracts the quotient back: it is a MODULO
```

The division is intermediate. Hex-Rays rendered `% 16` correctly, and "correcting" it to `/ 16` would
have introduced an error rather than removed one.

### `MapView_DrawUnitFacingArrow @ 0x1b7bc` — the marker a shot leaves on the map

Ported in `src/pufo/game/pufo_isodraw.c` behind `IsoArrowGate`, tested by `TestFacingArrow` in
`tests/test_isodraw.c` (46 checks).

One gate for everything: `pCell->bFlags29 & 0x20`, the shot-trace flag. `Tact_ComputeAimVector` sets
it on the origin cell (0x305A4) and `Map_ClearShotTraceFlags` clears it over the grid, so this function
only runs on cells a shot actually touched. Behind that gate the three branches are exclusive, in the
order the ARM tests them:

1. **`TactState + 0x111E1` non-zero** (0x1B7DC) — the fire animation is running. The glyph is muzzle
   smoke, `nSmokeSprBase + nPhaseCounter % 4 + 56`, at the unit's own facing offset
   `(nDrawX + Soldier.nDrawOffX, nDrawY + nObjParam + Soldier.nDrawOffY)`. `pUnit` is tested after the
   flag, so a flagged cell with no unit draws nothing. The flag is unnamed in IDA (`gap_70112[1]`);
   move command 0x12 sets it together with `nPhaseCounter = 0` (0x4F5B0), command 0x10 clears it, and
   `Tact_ProcessFallAction` writes it at 0x52BFC.
2. **`bFireStatus1` (Map+0x51) non-zero** — walk the ten-entry multi-target roster held in
   `adMtX / adMtY / adMtZ` at `Map+0x13EE8 / 0x13F38 / 0x13F88` (doubles, stride 8), with the cursor in
   `IsoDrawCtx + 0x114` (IDA `nRosterAux2`). An entry is drawn when `anAimRow[n] > 0` and its voxel
   coordinates floor to this cell: `X/16 == nX`, `Y/16 == nY`, `Z/12 == nZ`. The glyph is
   `LOWORD(anAimRow[n]) + nBulletsSprBase - 1`.
3. **`bTrajHitStop` (Map+0x1406D) non-zero** — the thrown item's own floor-object glyph,
   `pArrowThrownItem->pObj->aItemQty[50] + nFloorobSprBase - 1`, at the end of roster entry 0. No cell
   compare, no aim-row test. `pArrowThrownItem` (was `unk_127A28`, Map+0x14070) is written by
   `Tact_SimulateThrowArc` at 0x319F4, which stores the `TactItem *` parameter and sets the flag in the
   same block — that is what makes the two a pair, and it is also why the missing null test at 0x1C1F4
   is latent rather than live.

**The projection is the same iso rotation as `MapView_DrawUnitHeightGlyph`, with different constants.**
The three operands are again the remainders `A = X - 16*(X/16)`, `B = Y - 16*(Y/16)`,
`C = Z - 12*(Z/12)`, stored as doubles at 0x78 / 0x80 / 0x88, and the immediates are the same family:

```
roster branch  nHeightOff = (A*sin*scale + 16.0) - (B*sin*scale)
               nHeightAux = ((B*cos + A*cos)*scale + 40.0) - 16.0 - 2.0*C
throw branch   nHeightOff = (A*sin*scale + 16.0) - (B*sin*scale) - 16.0
               nHeightAux = ((B*cos + A*cos)*scale + 40.0) - 16.0 - 2.0*C - 20.0 - 8.0
```

The height glyph's `+2.0 - 2.0` pair is `+16.0` here, and the roster branch drops the `-20.0 - 8.0`
tail. Simplified with `sin*scale = 1.0` and `cos*scale = 0.5`: the roster gives `A - B + 16` and
`(A + B)/2 - 2C + 24`, the throw gives `A - B` and `(A + B)/2 - 2C - 4`.

**The nudge table is stored twice.** `byte_A9290` (x) and `byte_A92D0` (y) are 64 signed bytes each and
byte for byte identical — 13 non-zero entries, all negative, from -1 to -3. They are indexed
`10 * Map.nRowId + n`, so the row is the aim class and the column is the roster entry. The port keeps
one copy, `g_abArrowGlyphOff`, and `audit_tables.py` checks both image addresses against it; that is
what proves the duplication rather than asserting it. `nRowId` is the raw aim class id
`Tact_ComputeAimVector` stores at `Map+0x14088` (0x305C0) with no clamp, so an id above 5 reads past
the table in the original too — recorded at 0x1BD18.

**One correction to the notes above.** The `MOVS / ADDMI #0xF / ASR #4` idiom is truncating division,
not floor: for `v < 0`, `(v + 15) >> 4` equals `v / 16` exactly, so -17 gives -1 and not -2. The modulo
reading in the previous section stays right — C's `%` is the truncating remainder — but the *quotient*
truncates as well, which is what `Z/12` already did. The port's helper is named `Ball_FloorDiv16`; the
arithmetic is correct and the name is not, recorded at 0x1B8EC.

Deviations, both recorded in `docs/parity/bugs.tsv`: the return value is register residue and
`MapView_DrawIsoLayer` drops it (0x21FCC reloads `pIso` immediately), so the port returns the number of
glyphs drawn; and the port keeps one copy of the nudge table where the image keeps two.

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

### `MapView_DrawIsoLayer @ 0x21e2c` — the tactical map frame

Ported in `src/pufo/game/pufo_isodraw.c` behind `IsoLayerEnv`, tested by `TestIsoLayer` in
`tests/test_isodraw.c` (25 checks). It is the only caller of the per-cell chain, so it is also the
first place where all seven per-cell gates have to exist at once; `Render_Frame @ 0x22310` will own
that env when it is ported.

**Two passes per layer, not one.** Pass 1 calls only `MapView_DrawCellFloorGlyph`; pass 2 calls the
other eleven. That is why the floor is under the units rather than interleaved with them, and it is
also why the two loops look identical: the only thing they share besides the gate is the fallback.

**The fallback is layer 0 only.** When `MapView_bResolveIsoCell` fails, both passes draw the blank
group glyph at `fontSys + 0x00` with palette 0 — but only when `nLayer == 0`. The ARM tests
`LDRB R3,[pIso,#0x38]; CMP R3,#0; BNE` after the failed gate (0x21F04 / 0x21FEC), so upper layers
leave an unresolved cell completely empty. With `nDrawY = 8 * (nTileY - 3 * nLayer - 4)` that is easy
to miss: on layer 1 the first in-range row is `nTileY = 7`, which lands at the same y as layer 0's
`nTileY = 4`, so the test has to compare positions rather than count draws.

**The frame tail, in order.** `MapView_DrawViewportGuide`, `TactHud_DrawSelectedUnitPanel`, the
multi-target bars, `StatusMsg_DrawStack`, a colour pair, `UiControl_BlitActivePad` plus the pad's
centred label, and finally the cover.

The bars are the only arithmetic worth recording:

```
column   207, fixed
row      300 - 18 * i - (deploy panel open ? 17 : pHudPad->uHeight)
fill     30 by 16, colour index (u8)(10 * Map+0x37 + 128)   -- stored with STRB, so it is a byte
label    itoa(i + 1), centred: 207 + (30 - Text_MeasureWidth) / 2
cap      five, whatever Soldier.nMultiTarget says
```

The cover is a full 240 by 320 blit of index 55 at key 128, skipped when `Map_nUiScreen` is 0 or 13 —
the two screens that already own the frame.

**Three original defects, all in `bugs.tsv`.** `Unit_bIsOperative` is called with `Map.pSelUnit` with
no null test even though `StatusMsg_DrawStack` tests the same pointer a few instructions later
(0x22080). The pad label is copied from `aTextRecs + 0x35A7` into `IsoDrawCtx + 0xAC` byte by byte to
its NUL with no bound, and a text record is 205 bytes against an 84-byte buffer (0x22254) — the port
bounds it with `snprintf`. And `StatusMsg_DrawStack` is handed the draw context where it wants the
Map, so its scratch (`nStatusDrawY`, the loop cursor) lands in `IsoDrawCtx` (0x22190) — the port gives
it the real owner.

Four globals this function reads are now named: `nTileCountX` (Map+0x34) and `nTileCountY` (Map+0x35),
written by the camera-scroll input handlers; `bDeployPanelOpen` (Map+0x15110); and `pActiveLookCtl`
(`dword_1571E4`), the control `Input_OnPointer` last touched.

### `MapView_DrawMinimap @ 0x1ed4c` — the look-mode overview

A sibling of the iso layer rather than a callee: `Render_Frame @ 0x22310` calls it when `Map_nUiScreen`
is 2, and it reuses the same `IsoDrawCtx` (nTileX/nTileY/nLayer/nPal/pUnit/nScratch/nScratch2/szText)
while reaching none of the per-cell chain. Ported in `src/pufo/game/pufo_isodraw.c` behind
`IsoMinimapEnv`, tested by `TestMinimap` in `tests/test_isodraw.c` (37 checks).

**The cell passes are dead in the shipped beta.** The loop bounds are `Map_nMinimapSpanX >> 2` and
`Map_nMinimapSpanY >> 2` (Map+0x30 / Map+0x32, u16), and no instruction in the binary ever stores to
them — the only ten references are LDRH reads here and in `MapView_EnterLookMode @ 0x163d4`, which
reads them as `>> 1` for its camera-centre input and gets 0 there too. The shipped minimap therefore
draws only the cursor pad, the label and the three controls. The port reproduces the arithmetic, takes
the spans as env inputs, and the test pins both the dormant case and an explicit span.

**The scroll clamp.** `Map_nMinimapScrollX` / `Map_nMinimapScrollY` (Map+0x4EAC / 0x4EB0) are clamped
max-first then min: `40 * (nMapDimX - 3) / 4` and `40 * (nMapDimY - 4) / 4` as ceilings, -30 and -40
as floors, each comparison against the value the previous clamp left. The ARM's
`MOVS / ADDMI #3 / ASR #2` is truncating division, so C's `/` reproduces it. `Input_OnLeft` /
`Input_OnRight` pan X while screen 2 is active, `Input_OnPointer` drags both, and
`MapView_EnterLookMode` seeds them from the iso camera offset plus the camera-centre bytes
(Map+0x4A40/41, written by `MapView_SetCamOffset`) minus the visible half-extent 30/40.

**Pass 1** (layer 0, and it runs even when `nLayerScratch` is 0) paints every valid cell with one
ground glyph, `nScangSprBase + (sprite2->uUnk20 + 35)` — the sprite is the fixed record at flat index
2, so the pass paints a uniform ground — with the cell's `bUnk56` as palette, at
`4 * (tile - scroll)`. The lookup result is dereferenced with no null test (0x1EEA4).

**Pass 2** walks `nLayerScratch` layers (validity still tested at layer 0 — the bounds are
layer-independent) and draws per cell: the four sprite records in order (floor, wall A, wall B,
ceiling), each as `nScangSprBase + (uUnk20 + 35)` and skipped when `uUnk20` is 0, with palette
`bUnk56` when `bFlags29` bit 4 is set and 255 otherwise; the item marker
`nScangSprBase + (nMapPhase / 3 + 9)` when the cell has an item list AND `bFlags29` bit 4; and the
occupant marker — a large unit (`byte_A7B80[4*type+1] == 1`) picks its glyph by how its home cell
(`unit + 0x10`, nX@0x1A nY@0x1B) sits against the drawn one (same cell `4*(phase/3+3)`, same column
`+14`, same row `+13`, neither `+15`, plus 12 when `bStatus` is not OPERATIVE), a small unit uses
`phase/3` plus 3 when not operative. The markers always use palette 0.

**The furniture.** The `minimcur` pad (`pMinimapCursorPad`, the only centre-origin pad
`Tact_LoadUiPads @ 0x3b068` loads, cleared by `Tact_Shutdown`) blitted at key 0x3F; the label —
`aTextRec17` (`aTextRecs` record 17, 0xB38D0 + 205*17, loaded from lang.dat and never rewritten: the
copy at 0x1F4DC is its only reference in the whole binary) copied to its NUL with no bound
(ORIGINAL BUG, 0x1F4DC) with the decimal `nLayerScratch` appended at `szText[50]` — drawn at (0,0)
after the white/grey colour pair; and the three screen-2 controls at `pBank + 0xC40` (widgets 98..100,
the same bank the iso panel re-bases).

**Context layout fix.** The scratch dword the minimap stores per glyph and reads back as a u16 sits at
`IsoDrawCtx + 0xFC`, which is why `szText` is 80 bytes (0xAC..0xF3) rather than the 84 previously
assumed; the IDB struct already had `nScratch` at that offset. Five globals named this step:
`Map_nMinimapSpanX/Y`, `Map_nMinimapScrollX/Y`, `pMinimapCursorPad` (`unk_1571F0`) and `aTextRec17`
(`unk_B466D`).

### `MainMenu_Draw @ 0x1dbb8` — the start screen

Ported as a new module `src/pufo/game/pufo_mainmenu.c` behind `MainMenuEnv`, tested by
`tests/test_mainmenu.c` (25 checks). Another `Render_Frame` callee, and the draw side of the start
menu whose buttons (`MenuCtl0..3`, `MenuCtl8..14`) and teardown (`StartUi_Shutdown`) were already
ported in `pufo_menubtn.c`.

**The layout is chosen by the list mode dword at 0x157188** (`MainMenu_nListMode`, named this step) —
the same value `Menu_OpenSlotList` clears, `MenuCtl1_OnClick` sets to 2, `Menu_LoadSlot` sets to 3 and
`SaveLoad_Slot` sets to 1:

| mode | rows | labels |
|---|---|---|
| 0 | 4 menu rows at x 50, w 140, y 115 + 22*i | `aTextRecs` records 1076..1079 |
| 1 | the same four rows | records 1080..1083 |
| 2, 3 | 7 slot rows at x 25, w 190, y 65 + 22*i, then `MainMenu_pStartCtl` and its centred label | the 7 x 40-byte block `SaveSlot_FormatList` writes through `MainMenu_pSlotCtx`, plus record 1083 |
| other | none | — |

**Each row is five keyed blits.** A body `(x, y, w, 18)` at index 0 with colours (87, 94) and four
1-pixel strips at index 1 with colours (159, 171) — top at y-1, bottom at y+18, left at x-1, right at
x+w — all through `SoftSurf_BlitSrcRect`, so the row's key byte blends them over the backdrop. The key
comes from `MainMenu_anRowKeys` (0x1568F0, 7 dwords) and is forced to 255 when `MainMenu_abRowDisabled`
(0x156AB0) is set. The same key scales the row's text colour: the cyan pair (26,244,252)/(13,122,126)
times key/256. The original computes each channel with `itod / muld / dtoi` against six constants that
decode to exactly 26/256, 244/256, 252/256, 13/256, 122/256 and 126/256 — so the products are exact
binary fractions and `dtoi` is C's truncation.

**Mode 3 is the "loading" frame:** it writes 40 into `anActionPhaseSave0[112]` (0x128AA0, named) and
draws a one-line `UiModal_DrawDialog` 150 wide from text record 1102 with no buttons. Every mode ends
with the white/grey pair and `pszGameVersion` — the literal "V1.26" at 0xAAD28 — drawn at (5, 300).

**Labels are text records.** `unk_E9674 / unk_E99A8 / unk_E9C0F` are `aTextRecs` records 1076, 1080 and
1083 (0xB38D0 + 205*n) and are now named `aTextRec1076/1080/1083`. The slot labels are not text
records: they are 40-byte strings `SaveSlot_FormatList @ 0x45904` writes through the context pointer
kept at `MainMenu_pSlotCtx` (0x156938).

Ten globals named this step: `MainMenu_nListMode`, `MainMenu_pBgGlyph` (0x1568D8, the backdrop block
`GameUI_InitStartup` allocates and `StartUi_Shutdown` frees as `pBlockA`), `MainMenu_anRowKeys`,
`MainMenu_abRowDisabled`, `MainMenu_pStartCtl` (0x1571B4, the standalone control), `MainMenu_pSlotCtx`,
`anActionPhaseSave0_112`, `aTextRec1076/1080/1083` and `pszGameVersion`.

### `GameUI_InitStartup @ 0x38a28` — the start menu before the first frame

Ported in `src/pufo/game/pufo_mainmenu.c` behind `GameUiStartupEnv`, together with
`UiControl_LoadPads @ 0x1a604` (`pufo_control.c`: free the three pad slots, then load each
non-empty descriptor name as a `PadImage` in mode 1 — the mode is fixed 1, not the control's
mode). The order is the original's own: reader clear, `Font_Init`, eleven click handlers into
controls 0..3 and 8..14 (4..7 keep the static block's values), fifteen `UiControl_Setup` calls
from the `a2s` descriptors at 0xA7CF8 (xOff 50; yOff 115/137/159/181 for rows 0..3 and 65..197
step 22 for the seven slot rows; all pad names empty; mode 2) with 140x18 extents,
`startg.pal` into slot 20 and `menu.pal` into slot 1, `startg.pad` into `MainMenu_pBgGlyph` and
`emptyb.pad` into 0x1568EC, glyph base set to the running frame count **before**
`Font_LoadSpr("font")`, `Font_BuildIndex`, zero the seven row keys / disabled bytes / key
sources, build the standalone control (desc "V", y 230, pads emptyb1/cemptyb1/empty, click
`EmptyBtn_OnClick`), list mode 0, **`Map_nUiScreen = 17`** — `Render_Frame`'s `MainMenu_Draw`
branch — then the "tact" samples, music track 0, both volumes and `StartTimers_B`.

**The row flicker** is a `PocketUFO_WndProc` fragment at 0xA0AC8 inside the WM_TIMER 0xC352
branch (screen 0x11), ported as `MainMenu_TickRowKeys`: armed rows get key `32*acc-1` (acc is
the anim phase, plus one `Game_RandU16(8-phase)` roll while the phase is under 4; 0xA0AEC
re-seeds the accumulator from the phase every row, so the flicker never reads the global at
0x161A38 back -- but it is NOT write-only: BasePlaneScreen_DrawStatLabelA/B (0x79E00 / 0x79FC4)
read it as their panel row count), unarmed rows a steady 200; then a 1-in-400 roll either clears this row's source (a
further 5% chance, armed rows only) or zeroes every source and re-arms only this row.

**Three font-path facts the first rendered frame exposed:**

1. **Text colour is a palette-base trick.** `Font_BlitGlyphToSoft @ 0x9ec48` picks the glyph
   palette as slot `Map_nGlyphPalBase + nPal` (renamed from unk_1183F4; `nPal` clamped to 19).
   `Text_DrawString @ 0x2d798` saves that dword, **forces it to 2** for the duration of the draw
   (0x2d7bc) and restores it (0x2d818). `Gfx_PackRgb565Pair @ 0x2d82c` writes its packed colour
   pair into palette slot 2 entries 33 (`unk_147B54`, the r1/g1/b1 pack) and 27 (`unk_147B3C`,
   the r2/g2/b2 pack, also the return value) — the two globals are not scratch, they are those
   palette entries. Measured over FONT.SPR, the glyphs use only indices 0 (transparent), 27
   (main, 228 px over A/V/i/a/а/е/space) and 33 (secondary, 90 px). So forcing the base to 2
   routes every glyph through the pair the caller just packed: the menu rows get the cyan pair
   scaled by the row key, the version stamp gets the white/grey pair. `Render_Frame`'s default
   branch writes 34 into the base and `Geoscape_InitUI` writes 3 — those are the non-text bases.
2. Glyph run byte 0 is **transparency**: the ARM at 0xa4288 gates the pixel write on `*v5 != 0`;
   the pen advances regardless. The port previously wrote index 0 as a real pixel.
3. `GX_PresentFrame @ 0x9ea7c` arms the blit target through `Render_SetTarget(240,320,2,
   pitchX,pitchY,framebuffer)` → `Blit_SetTargetParams` writes `blitState+0x20`. That field is
   what `SoftSurf_BlitGlyph_Inner @ 0xa41d4` gates every glyph draw on (`nPixelMode != 0`), so
   without the call no text ever appears. The Win64 shell calls `Render_SetTarget` in
   `App_PresentLoadedSurface` for the same reason.

**Visual verification:** `pocketufo --dump-frame <path>` runs `GameUI_InitStartup`, 40 anim
ticks, one `MainMenu_Draw` frame, writes the 240x320 RGB565 surface as a P6 PPM, and exits. The
dump shows the backdrop, the four teal rows with key-scaled cyan labels (lang.dat text records
1076..1079), the white/grey "V1.26" stamp and the SMK logo — the start menu as the original
draws it.

### `Render_Frame @ 0x22310` — the frame dispatcher

Ported as `src/pufo/game/pufo_renderframe.c` behind `RenderFrameEnv`, tested by
`tests/test_renderframe.c` (18 checks). Every callee except `Geoscape_Draw (0x8c180)` was
already ported, so the env carries the dispatch scalars and the per-branch sub-envs;
`Geoscape_Draw` and the `GX_Present (0x27788)` thunk (a 4-byte jump to `GX_PresentFrame`) are
injected hooks.

**The geoscape branch returns early** (0x22328 / 0x22330): when `unk_1139FC` (Map+0x44) is set,
`Render_Frame` returns `Geoscape_Draw`'s value and never reaches `GX_Present` — `Geoscape_Draw`
presents itself. The three title/menu screens (0x10 animated titles, 0x11 `MainMenu_Draw`, 0x12
credits backdrop) each end with the present.

**The tactical default branch** ticks the cursor direction, writes 34 into `Map_nGlyphPalBase`
(0x22370 — the non-text glyph palette base), then dispatches: screens {0,3,5,7,8,9,0xA,0xB,0xC,0xF}
draw the iso layer when `unk_138E68` is set, with per-screen HUD panels (roster 3, status text 5,
labeled pads 7, modal panel right 0xA / left 0xB — the same body with records 1103/1102 — top bar
0xC, action buttons 0xF); 1 inventory, 2 minimap, 4 command pads, 6 unit markers, 0xD status
banner, 0x13 confirm dialog, 0x14/0x15 the Cydonia pad+panel. Then the modal dialog from
`apUiSlotsA[12..19]` when slot 19 is set, then the fade block: a positive `apUiSlotsA[1]`
dark-blits the whole 240x320 screen (key 20, blending toward `Blit_PackedColor(0,0,0)` = black);
otherwise a `apUiSlotsA[10]` counter under 10 blits the "enemystp" fade pad full-screen with
alpha 255 at 0 and `24*(10-counter)` otherwise, and the status stack draws when the iso layer is
on. Everything ends with `GX_Present`.

The shell now reaches the start menu through the dispatcher (`App_DrawFrame` builds a
`RenderFrameEnv` with the menu branch and the present hook) instead of calling `MainMenu_Draw`
directly.

### `Tact_DrawCommandPads @ 0x1fc8c` — the screen-4 status panel

Ported in `src/pufo/game/pufo_isodraw.c` behind `TactCmdPadEnv`, tested by `tests/test_cmdpads.c`
(42 checks). `Render_Frame`'s screen-4 branch: the selected unit's status panel.

**Backdrop and slots.** The 'soldier' pad (`pPadSoldier`, `unk_128AC0`, Map+0x15108, stored by
`Tact_LoadUiPads`) is blitted first. The two 32x32 pad slots sit at (5,2) and (39,2). The unit-type
gate at 0x1FCEC — types 4..8, 14..20 and 22, the craft family — skips the slots entirely. Otherwise
`uMissions <= 3` draws empty slots: a grey (0x96) 32x32 frame with a black 30x30 inside at each
position. Otherwise `nClass` 0..5 picks the pad pair from the three 'cs' pads loaded by
`Assets_LoadStartup` at 0x387f4..0x38894 (mode 0x12, origins zeroed there): 0=(cs0,cs0) 1=(cs1,cs1)
2=(cs2,cs2) 3=(cs0,cs1) 4=(cs1,cs2) 5=(cs0,cs2); class >5 draws nothing. The panel rewrites each
pad's own `nOriginX/nOriginY` to (5,2) then (39,2) before every blit (0x1FD84..0x1FE80), so the
loaded pads keep the last slot they were drawn at.

**Name lines.** `Unit_FormatDisplayName` mode 1 at (75,2). Mode 2 at (75,20) for the normal families;
for the craft family it is centred as `120 - Text_MeasureWidth/2` at y 24, and the original calls it
twice there (0x1FFB4 / 0x1FFC4) — the second call only feeds the width measurement and returns the
same `szDisplayName` buffer, so the port calls once.

**Eleven label/value rows.** Labels are `aTextRecs` records 56..66 (0xB65A8..0xB6DAA at stride 205 —
named `aTextRec56..66` this step; they are read straight from the loaded blob, never rewritten). The
value is built in `IsoDrawCtx.szText` (itoa into szText[50], copy down, append "/" or "/100", append
the second itoa). Rows: TU `uTu/auStat[0]` (45), stun `uStunOrArmor/auStat[9]` (69), HP `uHp/auStat[1]`
(93), `bKneelOrPrimed` alone (117) plus one 12x6 block per point at y 133, `auStat[2]` (141), morale
`bMorale "/100"` (165) with a fixed 200-wide bar and a `2*(99-morale)` remainder, `auStat[4]` (189),
`auStat[5]` (213), `auStat[8]` (237), and `auStat[7]` / `auStat[6]` (261/285) only while `bUnk422`.
The current/max bars are `max>>1` wide with a remainder `(max>>1) - (cur>>1) - 2` at `(cur>>1)+11` —
negative at full value, which the clip drops.

**The ruler is an original bug (0x20AD4).** After all the text, the function paints 12 rows x 24
columns of 5x1 ticks at y = 24*row + 45 — exactly the label rows — left ticks at x = 5*(col+1) with
index 5*(col+20), right ticks at x = 5*(col+23) with index 250 - 5*col, each with idx == mid == low.
It cuts a colour ramp through every label and value. Hex-Rays renders the right index as
`-6 - 5*col`, a sign-extension artifact of the STRB byte store; the ARM is `RSB R0, R2, #0xFA` =
250 - 5*col. The loop counters are the context dwords at 0x124/0x128 (now `IsoDrawCtx.nCmdTickCol /
nCmdTickRow`). Reproduced as written and recorded in `bugs.tsv`.

**Frame and controls.** Top 230x1 at (5,2), left and right 1x308 at x 5 / 235, index 0x96. Then bank
widget 101 (`pBank + 0xCA0` = 0xAD370, the bclose / bprevs / bnexts descriptors) always, and 102/103
only while `bLookCommitFlag` is zero. The original returns the control-loop counter; `Render_Frame`
ignores it, so the port returns void.

Five globals named this step: `pPadSoldier`, `pCmdPadCs0/1/2` (0x186C30/34/38, `PadImage *`) and
`aTextRec56..66`.

## `Tact_SelectTeamUnits (0x39744)`, decoded and ported (2883497, `pufo_tactctl.c:491`)

All four of its callees are ported (`Unit_ResetBattleState`, `Unit_PlaceOnMap`, `Tact_SelectUnit`,
`Unit_RecalcInventoryStat`).

The deployment position comes from `byte_A66B8`, a table of 104-byte records indexed by the selector byte
`unk_128ACC`:

```
nX     = 10 * Map_nCraftMapX + entry[0]
nY     = 10 * Map_nCraftMapY + entry[1]
nLayer = entry[2]
```

ARM reaches the three fields as `[R0,#-0x68]`, `[R0,#-0x67]` and `[R0,#-0x66]` off `byte_A66B8 + 104*sel`,
which is entry bytes 0, 1 and 2. A zero selector makes all three zero.

Every soldier gets `Soldier+0x1B4` (`nUnk436`) cleared first, unconditionally, before any acceptance test.

The mode byte `unk_128AD5` then decides who joins the team:

| mode | accepted when |
|---|---|
| 0 | `bUnk183 == unk_128ACB` |
| 1 | `nKind` equals the byte at `0x158890+79`, and `bUnk183` is not 254, and either it is 255 or `apCraft[bUnk183]->bAtBase` is set |
| 2, 3 | `bUnk183 == Map_nCraftIndex` |
| negative, or above 3 | unconditionally |

Accepted units get `Unit_ResetBattleState(pUnit, 0)`, `bStatus = 0`, `Unit_PlaceOnMap(nX, nY, nLayer)`, and
are appended to `apXcomUnits` while `nXcomUnitCount` increments.

One behaviour to carry over carefully: **a rejected unit ends the loop rather than being skipped.** The
rejection branch jumps straight to the count read, so a single unsuitable soldier in the roster truncates
the rest of the team. That is easy to mistake for a `continue` when reading the control flow.

If the count is non-zero the function then calls `Tact_SelectUnit(apXcomUnits[0])` and runs
`Unit_RecalcInventoryStat` over the roster.

Both loops walk their arrays through the IDB's base-advancing form — `v6->apSoldiers[0]` with `v6 += 4`,
`v10->apXcomUnits[0]` with `v10 += 4`. That is how IDA renders an index walk in this function and is not
an off-by-one or a repeated-first-element bug; I initially read it as one and corrected the IDB comment.

## `Tact_TallyCraftRecovery (0x344b4)`, decoded and not yet ported

Post-mission craft cargo tally, 556 bytes. The second argument is a dword out-param set to 1 when loot was
recorded.

Which craft the tally applies to depends on the mission type:

- **XCom base defence**: build a stack list of up to 20 craft indices by walking `apCraft` and recording the
  index of every craft whose `bHomeBase` equals the byte at `0x158848`. The count of those is the loop bound
  used per object.
- **any other mission**: the list has exactly one entry, `Map.nDeployCraftIdx`.

Then, for every object, it takes the first N entries of that list. For each craft index whose stored capacity
is non-zero, recovery is capped: if `uRecovCount` is at most the capacity, `uRecovCount` is written into the
object's per-craft quantity slot and `uRecovCount` is cleared; otherwise the capacity is subtracted from
`uRecovCount` and the remainder carries on.

The leftover then splits by mission type again:

- **XCom base defence**: the remainder is added straight into `ObjectRec.auPerBaseVal[byte at 0x158848]`.
- **otherwise**: if the remainder is non-zero it is stored into the record at `0x162030` indexed by the dword
  at `0x186410`, at `auItemQty[object index]`; that record's `bHasLoot` is set to 1, `uRecovCount` is cleared,
  and the out-param is set to 1.

Two things stop this being ported today, and both are recorded in the IDB comment rather than guessed
around:

1. The entry gate is rendered as `result[414].aItemQty[29]` and `result[32].gap_0[33]`. Both are
   base-advancing artifacts of the same kind that misled me in `Tact_SelectTeamUnits`, so the condition is
   not known until read in ARM.
2. The port owns no model for `0x162030` or `0x186410` — the loot record and its index, which belong to the
   geoscape state block rather than to the tact module. `0x158848` is modelled now: it is the mission's base
   index, loaded with LDRSB, and it enters the port as `TactCargoEnv.nBaseIndex`.

## Verified: the craft speed table, `StrategEntry_InitTravel (0x7f60c)`

The four fractional speed bases are soft-float `subd` immediates given as dword pairs. Unpacked and compared
against the port's table:

| class | dword pair | value | port `g_aCraftSpeed` | jitter roll |
|---|---|---|---|---|
| 0 | `639091134, 1078089043` @ 0x7F7BC | 36.6666 | 36.6666 | none |
| 1 | integer base 40 @ 0x7F804 | 40.0 | 40.0 | `RandU16(3)` |
| 2 | integer base 45 @ 0x7F83C | 45.0 | 45.0 | `RandU16(3)` |
| 3 | `-1827938081, 1079028393` @ 0x7F890 | 66.6666 | 66.6666 | `RandU16(5)` |
| 4 | integer base 71 @ 0x7F8D0 | 71.0 | 71.0 | `RandU16(5)` |
| 5 | integer base 80 @ 0x7F908 | 80.0 | 80.0 | `RandU16(5)` |
| 6 | `639091134, 1078646099` @ 0x7F95C | 53.6666 | 53.6666 | `RandU16(4)` |
| 7 | `-913969041, 1079301460` @ 0x7F9B4 | 83.3333 | 83.3333 | `RandU16(5)` |

The three scales match too: the speed floor at 0x7F9F4 is exactly `10.0`, the inactive-craft fuel scale at
0x7F714 is `0.016666666666666666` = 1/60, and the track-A scale at 0x7FA3C is exactly `0.02`. The jitter
roll table `{0, 3, 3, 5, 5, 5, 4, 5}` matches the per-class `Game_RandU16` bounds, and the base jitter is
`Game_RandU16(0x12)`, so 0..17.

These are the literal decimal values the original compiler emitted, **not** fractions like 110/3 or 200/3.
That matters: 36.6666 and 110/3 differ by about 7e-5, so writing the "obvious" fraction would have been a
silent parity drift.

Method note, because it bit me twice in one session: unpack these with a computation, not by hand. My own
hand conversion of `1078646099` gave a value around 53.17 and made me think the port's 53.6666 was wrong.
It is exact. `struct.unpack('<d', struct.pack('<II', lo, hi))` is the reliable route.

## Verified: the craft ammo group table, `Base_StripAllCraftCargo (0x69a38)`

`g_aCraftAmmoGroups[10]` matches the original exactly — kind, ammo objects, count and weapon object:

| ammo objects | weapon | `Craft_LoadStoresFromBase` kind | its `nSrc` |
|---|---|---|---|
| 36 | 1 | 1 | 0x24 = 36 |
| 37 | 3 | 3 | 0x25 = 37 |
| 38 | 4 | 4 | 0x26 = 38 |
| 39 | 6 | 6 | 0x27 = 39 |
| 40, 41, 42 | 7 | 7 | **0** |
| 43, 44, 45 | 8 | 8 | **0** |
| 46, 47, 48 | 10 | 10 | **0** |
| 49 | 11 | 11 | 0x31 = 49 |
| 50 | 12 | 12 | 0x32 = 50 |
| 51 | 13 | 13 | 0x33 = 51 |

`nSrc` is the first ammo object index for the single-ammo groups and **0** for the three multi-ammo groups.
That is not an oversight in the original; it is what the calls at 0x69CE4, 0x69DCC and 0x69EB4 pass.

The gate was worth re-reading because the IDB's own comment had it wrong. Both sides of the comparison read
`apObjects[obj]->aItemQty[craftIdx]`, which is what **that craft** carries. So the gate is

```
(sum of the group's ammo carried by this craft) < (weapons carried by this craft)
```

unsigned, and evaluated **before** any deposit — a group that already carries at least as many rounds as it
has weapons is left completely alone. The IDB comment said "craft-carried against base-stock", which is not
what the code does; corrected before saving. The port's gate was already right.

Deviation worth recording: the port adds guards the original does not have — it skips a craft whose
`bHomeBase` is outside 0..7, and it skips null entries in `apBases` and `apObjects`. The original
dereferences whatever those hold.

## Verified: the step table, `aDirDeltaXY @ 0xa7ae0`

30 bytes of signed triples, read as ten 6-byte chunks so the boundaries could be cross-checked. All ten
match `g_aStepDir` exactly:

| dir | bytes | delta | dir | bytes | delta |
|---|---|---|---|---|---|
| 0 N | `00 FF 00` | (0,-1,0) | 5 SW | `FF 01 00` | (-1,1,0) |
| 1 NE | `01 FF 00` | (1,-1,0) | 6 W | `FF 00 00` | (-1,0,0) |
| 2 E | `01 00 00` | (1,0,0) | 7 NW | `FF FF 00` | (-1,-1,0) |
| 3 SE | `01 01 00` | (1,1,0) | 8 up | `00 00 01` | (0,0,1) |
| 4 S | `00 01 00` | (0,1,0) | 9 down | `00 00 FF` | (0,0,-1) |

This is the table the step legality test `Unit_CanStepTo @ 0x27bb8` and the flood fill both dispatch on, so a
wrong entry here would corrupt movement and line of sight across the tactical layer. It is correct.

## Verified: the two 148-entry combat sound tables

`g_aShotSound` at `off_AA040 @ 0xAA040` and `g_aFireSoundBySortKey` at `word_AA168 @ 0xAA168`, both indexed by
`ObjectRecFull.uSortKey`, both 148 u16 entries. Both match the image exactly, entry for entry.

## The audit is now a tool: `docs/parity/audit_tables.py`

Reading byte tables by eye cost me a wrong commit this session, so the comparison is now computed. The script
reads `pufobeta.exe` directly, pulls each port table's initialiser list out of the source with comments
stripped, and diffs them entry by entry.

The address mapping was established by locating `aDirDeltaXY`: its ten signed step triples appear verbatim in
the file at offset `0x94AE0`, and IDA shows it at `0xA7AE0`. So **IDA_VA − 0x13000 = file offset**, which
agrees with the mapping recorded earlier for the `.data` section.

Current run:

```
g_aShotSound               000AA040  148 entries  exact match
g_aFireSoundBySortKey      000AA168  148 entries  exact match
g_aStepDir                 000A7AE0   30 entries  exact match
g_aUnitTypeFlag0           000A7B80   24 entries  exact match
g_aUnitTypeIsLarge         000A7B81   24 entries  exact match
g_aUnitMuzzleOffset        000A9768  1104 entries  exact match
s_aHitRect                 000A8DD8  175 entries  exact match

7 tables checked, 0 mismatched
```

`g_aUnitMuzzleOffset` is the one worth naming: 23 unit types × 2 selections × 8 facings of i16 triples,
2,208 bytes at `unk_A9768`, and it is the largest table in the port that was transcribed out of the binary.
It is byte-for-byte correct. `s_aHitRect` is `aInvHitRects @ 0xA8DD8`, 25 records of 14 bytes.

Registering a multi-dimensional table needs the symbol's full declarator — `g_aUnitMuzzleOffset[N][2][8]` —
so the tool matches repeated bracket groups rather than one.

The unit type flags are registered with a stride of 4 because the image stores them as 24 rows of 4 bytes
while the port keeps two separate 24-entry arrays. That stride is exactly what got mis-transcribed by hand
earlier in the session, so it is the case worth having a tool for.

## The gamma setting, and what the palette ramp actually does

Ported: `Tact_LoadPalettes @ 0x377ec` (760 B), `Tact_BuildPaletteLuts @ 0x3734c` (1,184 B), and the five
`MenuBtn_SetGamma` handlers at `0x19070`, `0x190d0`, `0x19130`, `0x19190`, `0x191f0` (5 × 96 B).

**`unk_128AB8` is `Map_nGamma`, Map+0x15100.** Its only writers are the five gamma buttons, which store
0..4, plus `Map_InitAndLoadRegistry @ 0x22648` and the options screen. Its readers are
`Tact_BuildPaletteLuts` and `Gfx_PackRgb565Pair @ 0x2d82c`. The LUT divisor is

```
0.1 * gamma + 0.7        gamma 0 -> 0.7   1 -> 0.8   2 -> 0.9   3 -> 1.0   4 -> 1.1
```

so **gamma 3 is the neutral setting** and the pass is the identity there. Gamma 0 brightens by 1/0.7,
gamma 4 dims by 1/1.1.

The five gamma buttons are **bank widgets 65..69**, and the flag they set is `UiControl.nState` — the
addresses the originals write (`nGammaSel0..4` in the IDB now) are `pBank + 32*w + 10` for w = 65..69, and
10 is `nState`. There is no separate selected-state table.

### The ramp is per-channel, and the negatives cancel

`Tact_LoadPalettes` loads eight palettes and then derives 20 slots (34..53) from `tactical.pal` at slot 34.
For each `i` the scale is `1 / (0.07 * i * i + 2)`. The inner loop splits each colour into three 5-bit
fields, multiplies **two of them by negative scale** and one by positive scale, converts each with `dtou`,
then recombines as `lo - 32 * (a + 64 * b)`.

Read as arithmetic instead of as register moves, the same pass is

```
out = floor(2s*blue) + 32*floor(2s*green) + 2048*floor(2s*red)
```

Two things that matter:

- It is **per channel**, not per colour. Each field is truncated on its own, so no carry crosses a field
  boundary. Scaling the whole 16-bit value once gives different results.
- `dtou` on a negative double must **wrap**, not clamp. Slot 34 is both the source and the destination at
  `i = 0`, where the scale is exactly 0.5, so the pass has to be the identity or it would destroy the very
  palette the rest of the ramp reads from. Only the wrapping reading makes it the identity. That is
  evidence, not assumption.

Slot 53 (`i = 19`) is forced to zero at `0x37A58`, so the ramp ends in black.

The port reproduces the original's operations literally (negative scale through `dtou`) and the test checks
the result against the algebraic form above, on the **real `tactical.pal` from main.dat**, across all 256
entries and all 19 derived slots. Two independent readings of the same original have to agree 4,864 times.

### A Hex-Rays member error worth naming

`Tact_BuildPaletteLuts` reads the display format from **`0x1883AC`**, not the GX format at `0x188398`. ARM
at `0x37350` loads `[R0,#0x14]`. Hex-Rays renders it as `MEMORY[0x188398].ffFormat`, which is the wrong
member of that block — the same operand `Gfx_PackRgb565Pair` and `Palette_Load` use. Corrected in the IDB.

### Original bug found here

`0x37718`: the 24-bit branch reads `HIBYTE(w)` and `(u8)w`, so it only ever sees the low 16 bits of a
palette entry, and it writes back 16 bits. `Palette_Load` packs the `0x100` format as `R | G<<8 | B<<16`,
so on a 24-bit display this pass drops the whole blue channel out of every palette. Reproduced, and pinned
by a test that feeds in `0x00AABB1234` and asserts the entry comes out `0x00001234`.

## `Map_PatchCraftSprites @ 0x25ab4`, and the X/Y convention it exposes

Ported (952 B). Runs only on a two-layer map: it sets twelve `abPal` bytes to 6 on craft sprites +13 and
+3, five on +21, zeroes the u16 at record offset `0x14` on sprites +42..45, and stamps `pWallSprA` on
twelve layer-1 cells.

**The cell coordinates are swapped, and that is a convention rather than a bug.** The original puts
`10 * nCraftMapY + row` into the slot every other reader calls `nMapX`, and `10 * nCraftMapX + col` into
the `nMapY` slot. `MapView_DrawUnitHeightGlyph`'s gate does the same thing — `nMapX` against `nFireTgtY`,
`nMapY` against `nFireTgtX`. Two independent sites agreeing is why this is recorded as convention.

Hex-Rays hides it. At `0x25BD4` it renders `v10 = v10 + 86329` and then reads `v10->bTactFlag`, which reads
like a flag. `0x1139B8 + 86329 + offsetof(Map, bTactFlag)` lands on `Map_nCraftMapX` — the operand is the
craft tile column.

`fontSys_nCraftSprBase` (`unk_15826E`, font block +0x06) named in the IDB; it is the craft sprite group the
whole function indexes through.

The test is worth keeping because it cannot be satisfied by an off-by-one or a swapped index: twelve
distinctive cells sit at the decoded positions, twelve decoys sit at the same twelve pairs with the
coordinates the other way round, and every other grid slot points at one shared sink. The two index sets
are disjoint, so the wrong order writes decoys and the sink instead.

## `Soldiers_Reload @ 0x430cc` — the allocation half of a load

224 bytes, ported as `Soldiers_Reload`. It is easy to mistake for a load and it is not one:

1. free `apSoldiers[0 .. nSoldierCount-1]` with `operator delete`;
2. `DatFile_FindChunk("soldiers")` and read **only the 2-byte count**;
3. allocate that many soldiers through `Soldier_ctor`.

The bodies come from `Save_ReadSoldiers @ 0x416bc`, which runs afterwards. So after this the roster is the
right size and every entry is a fresh empty soldier. The test pairs them: reload, then read, and asserts the
written values land in the objects the reload allocated.

Two things worth naming. The store at `0x43188` writes the **result** of `Soldier_ctor` rather than the
pointer that was allocated — harmless, because `Soldier_ctor` returns its argument, but it is why the
pseudocode reads like a type error. And the free loop nulls nothing and leaves the count alone, so a failed
`FindChunk` or count read leaves `nSoldierCount` naming memory that is already gone. That is ledger row
`0x430D8`; the port nulls the slots.

## `TactItem_bTryReloadClip @ 0x4b6a4` — already ported, and how I nearly duplicated it

This one was **already** in the port as `TactItem_TryReloadClip` in `game/pufo_tactui.c`, with six checks in
`test_tactui.c` and a row in `renames.tsv`. A scan that asks "which original function names do not appear in
the sources" listed it as unported, because the port renamed it. I wrote a second implementation before
checking, and removed it. The name-based scan is the same weakness `audit_names.py` exists to work around;
the fix is to check `renames.tsv` before porting anything the scan reports.

What the function does, since it is worth having written down. Two records are involved and they do different
jobs: the gate tests the **weapon's** `bAmmoCap` at `ObjectRec + 0xAE`, while the value written into
`TactItem.nAmmo` comes from `apObjects[weapon.anAmmoTypeId[0]]->bAmmoCap`, i.e. the **ammo** record's cap.
Setting only the weapon to zero still loads the ammo cap — which is exactly why the port injects both numbers
instead of reaching them through globals. The action spent is `Soldier.bHumanoidInv` at `+0x1A2`, decremented
with `ADD R3, R3, #0xFF` on a byte and only when already non-zero (`BLS` at `0x4B6CC`), so it cannot wrap.
The return is not "did a reload happen": an item that already had rounds returns true without spending
anything, and a weapon with `bAmmoCap == 255` returns true even when it is empty and nothing happened.

The first argument is genuinely unused — ARM overwrites `R0` with `#0x1A2` before reading any incoming value.

## `Soldier_CreateAndClassify @ 0x22ba0` — and a fallback that cannot be reached

Ported as `Unit_CreateAndClassify` (624 B), with the classification split out as `Unit_ClassFromStats` so the
gates can be aimed at chosen stats. `unk_186554` renamed in the IDB to **`g_nNextSoldierId`** — the roster id
handed to `Soldier.nUnk440`, which `Soldier_RemoveById` searches on and which the save file persists.

Hex-Rays renders each gate as a bool that is conditionally recomputed, which hides a plain OR:

```nv12 = (A - B <= 5);  if (A - B > 5) v12 = (A - C <= 5);
```

so `!v12` means `A - B > 5 AND A - C > 5`. Read that way the whole classification is just "one average
dominates the other two":

| class | condition |
|---|---|
| 0 | `A - B > 5` and `A - C > 5` |
| 1 | `B - C > 7` and `B - A > 5` |
| 2 | `C - B > 0` and `C - A > 0` |
| 3 | balanced and `|A - B| < 10` |
| 4 | balanced and `|B - C| < 10` and `B > A` |
| 5 | balanced and `|A - C| < 10` and `A > B` |

with `A = (auStat[8]+auStat[9]+auStat[5]+auStat[0]) >> 2`, `B = (auStat[3]+auStat[4])/2`,
`C = (auStat[6]+auStat[7]+auStat[2])/3`.

**The fallback at `0x22DE8` is dead code.** If nothing fires, the original draws `Game_RandU16(3) + 3`. It
cannot happen: sweeping A, B and C over −200..300 leaves **zero** unclassified triples, and the reachable
averages from `Unit_Generate` are A 35..64, B 35..64, C 7..62. So soldier classes are fully deterministic,
which is worth knowing before anyone treats them as randomised.

Two smaller things reproduced rather than tidied: the id is assigned **before** the counter is bumped, and
the function returns the **incremented** counter, so callers receive the next id rather than the one handed
out; and the count is a u16 with no bounds check on the roster array. The missing `operator new` check is in
the ledger at `0x22BB8` — the original stores the null and dereferences it one instruction later, so the port
departs rather than reproducing a crash.

## `Tact_EquipTeamFromCraft @ 0x3be24` — decoded, deliberately not ported yet

1,836 bytes, 82 basic blocks. Every callee it needs is already in the port (`Inv_HandlePointer`,
`MapCell_ListRemove`, `MapCell_ListInsertByPri`, `Unit_RecalcInventoryStat`, `TactItem_ctor`,
`aInvHitRects`), so it is unblocked — it is a size problem, not a dependency problem. Recorded here so the
port is a transcription job rather than a re-derivation.

It loops over `Map.nXcomUnitCount` / `Map.apXcomUnits` and splits on `Soldier.bUnk961` (Soldier+0x961), an
already-equipped flag. Set: re-run the two 25-slot inventory passes. Clear: equip from the craft, then set
the flag at `0x3C510`.

**The 25-slot walk.** Hex-Rays renders the cursor advancing by 2 and reads `auUnk962[0]` each time, which
means it walks `auUnk962[0..24]` — 25 u16 object indices at `Soldier+0x962`, one per inventory slot.
`abUnk1012[slot]` at `Soldier+0x1012` is the parallel array on the same counter.

- **Pass A**, slots whose object has `bTwoHanded == 0` **and** `nCategory != 3`: find a `TactItem` in the
  list at `0x157204` whose `pObj` matches and whose `nAmmo` is non-zero, set the pointer scratch at
  `0xAD714`/`0xAD718` from `aInvHitRects[slot]`, clear `nInvDragOffX/Y`, set `pSelUnit` and `pInvHitItem`,
  call `Inv_HandlePointer`, remove from the list, recalc. Then, if `bAmmoCap != 255`, ammo bookkeeping
  against `abUnk1012[slot]`: either build a fresh `TactItem` carrying the old ammo and reinsert it by
  priority, or swap ammo with a matching entry.
- **Pass B**, same 25 slots, gate `bTwoHanded != 0` **or** `nCategory == 3` **or** `nCategory == 5`: same
  drive and remove, no ammo bookkeeping.

**The equip branch is three table walks over candidate object indices.**

| table | entries | for | accept when |
|---|---|---|---|
| `unk_A62F0` | 13 dwords (index +4, ends at 52) | left hand | `4*(burden + weight + 3) / strength == 0` |
| `anAmmoTypeId[0..2]` | 3 | ammo for that weapon | `4*(burden + weight) / strength < 1` |
| `unk_A6328` | 6 dwords (index +4, ends at 24) | belt | `4*(burden + weight + 3) / strength < 2` |

All three divides are `__rt_sdiv`. The hit rect for ammo is **16** when the ammo object's `nFootW >= 3` or
its `nFootH != 1`, otherwise **4**; the belt always uses hit rect **10**.

**Layouts proven from ARM, not inferred:**

- the item-list container at `0x157204` has `pItemList` at offset **0x34**;
- `Map.apTactItems` is at **Map+0x23438** (`MOV R2,#0x23400` then `ORR R2,R2,#0x38`);
- `TactItem` is 24 bytes: `pObj` +0, `nIndex` +4, `nAmmo` +6, `pNext` +0xC;
- `ObjectRec.bWeight` at **+0xA5**;
- `Soldier.uInvBurden` at **+0x1A4**, and the strength stat used here at **+0xB2**.

**Hex-Rays artifact resolved.** At `0x3C0B0` it renders `v12 = &Map + 4 * ++nTactItemCount` and then
`v12[36109]`, which reads like an out-of-range slot. ARM adds 1 to the count and then loads from
`Map + 4*newCount + 0x23434`, which is `apTactItems[newCount - 1]` — the item stored a few instructions
earlier at `0x3C04C`. It inserts the just-created item.

## `Tact_EquipTeamFromCraft @ 0x3be24` — ported, and it equips soldiers with a parked pointer

1,836 bytes, ported as `Tact_EquipTeamFromCraft` in `game/pufo_invdrag.c`.

**The mechanism.** It writes a hit rect origin into `0xAD714` and `0xAD718`. Those two addresses *are*
`pBank+0x1044` and `pBank+0x1048` — the pointer position `Inv_HandlePointer` reads at `0x12290` /
`0x122A0`. It then re-tests at `ptr + drag + 10`. With the drag cleared that is `rect.x0 + 10`,
`rect.y0 + 10`, and for all 25 rects that point falls inside the very rect whose origin was written.
So the function equips a soldier by parking the virtual pointer exactly on each slot. No mouse is
involved, and the fact that it works for every one of the 25 rects is what confirms the reading.

**Three pickups when `Soldier.bUnk961` is clear**, each a table walk over candidate object indices:

| pickup | table | gate | how it is placed |
|---|---|---|---|
| left hand | `aTactHandObjects` (13) `6 11 5 9 3 2 4 7 8 1 12 13 10` | `nAmmo != 0` and `4*(burden+weight+3)/strength == 0` | assigned straight to `pHandL`, **no drop path** |
| ammo | `anAmmoTypeId[0..2]` | `4*(burden+weight)/strength < 1` | rect 16 if `nFootW >= 3` or `nFootH != 1`, else 4 |
| third | `aTactPackObjects` (6) `18 17 15 16 19 14` | `4*(burden+weight+3)/strength < 2` | rect 10 |

Both tables are now in `audit_tables.py` and match the image exactly.

**The third pickup is not a belt pickup.** It stores `aInvHitRects[10]`, and rect 10 is
`x 160..179 / y 160..179` — **backpack slot 0,0**. The belt rects are 16 through 24. My first test
asserted a belt landing and failed; the port was right and my expectation was wrong.

**Three original defects, all in the ledger.**

- `0x3C4C8` — reproduced. The third pickup stores the unit, the item and the pointer but never zeroes
  `nInvDragOffX/Y`, unlike `0x3BF38`, `0x3C1D8` and `0x3C3EC`. It inherits the footprint offset the
  previous re-test left, so a 2-wide item picked up just before leaves `-20`, the re-test lands at
  `150,150` which is inside no rect at all, and the slot is missed.
- `0x3C0F0` — not reproduced. The slot ammo swap is **unreachable**. The branch is entered only when
  `nIndex != abUnk1012[slot]` (`0x3BF9C`), and the match at `0x3C000` needs `nIndex == abUnk1012[slot]`
  with the same two operands. The second walk always runs to the end of the list and changes nothing.
- `0x3C2A4` — not reproduced. `auStat[8]` is the divisor of every carry gate, reached through
  `__rt_sdiv` with no zero test, so a strength of 0 is a divide by zero.

One layout detail worth keeping: `abUnk1012` at `Soldier+0x3F4` is a **byte** array while
`TactItem.nIndex` is a **u16**, so the compare at `0x3BF9C` is 16-bit against 8-bit.

## `Tact_DeployTeamAndHwps @ 0x39980` — ported

2,348 bytes, 64 blocks. Five phases.

1. Park every existing XCom unit at cell `(1,1,0)`.
2. Loop object indices **31..35**, the hardware store slots. `v4 = apObjects[i]->uRecovCount`
   (`ObjectRec + 0xCA`) is how many soldiers to spawn; `nUnitType = i - 27`, so types 4..8.
3. Re-place the units parked in phase 1, scanning deploy points for the first cell with no occupant.
4. `Tact_SelectUnit(apXcomUnits[0])`.
5. Only when `nMapLayers == 0`: build a scatter list and move every `apTactItems[j]` from the head of
   `apCells[0][0][0]->pItemList` onto layer-1 cells.

**`apBases[nSoldierCount + 8]` is a naming artifact, and it is one slot *inside* the roster.** ARM at
`0x39B48` is `MOV R2, #0x1194` then `LDR R4, [R3, R2]` over `Map + 4*nSoldierCount`. **`apSoldiers` is at
Map+0x1198**, proven from `Soldier_CreateAndClassify` at `0x22BE8`, which builds `0x1180 ORR 0x18 = 0x1198`.
So the address is `Map + 0x1198 + 4*(count - 1)`, which is **`apSoldiers[nSoldierCount - 1]`** — the soldier
just created. `apBases` at Map+0x1174 with nine entries ends at 0x1194, which is why Hex-Rays picked it.

The same addressing appears in the gate at `0x39A14`, where Hex-Rays renders
`apBases[nSoldierCount + 8][8].nUnk0 == 6` as if it tested a base record byte. ARM is
`LDR R3, [R3, #0x1194]` then `LDRB R3, [R3, #0xA0]`, and `0xA0` is `nUnitType`. **The gate is: the last
soldier in the roster has `nUnitType == 6`.** Since `nUnitType = object index - 27`, that is object 33, so
deploying object 33 makes the *next* object index skip its ammo computation. No null dereference and no
off-by-one — the read is inside the roster, not past it.

**A hardware soldier is named after the object it carries**: the name is copied from the ObjectRec's own
first bytes at `ObjectRec + 0`.

Two stat presets. Objects 31, 32, 33 get `auStat[0]=70 [1]=90 [2]=110 [3]=20 [4]=60 [8]=60 [9]=70` and
`bUnk248 = 40`; objects 34, 35 get `100/90/110/30/60/60/100` and `bUnk248 = 55`. Both get `bMorale = 100`.

**Object index 33 has no ammo computation.** The switch has cases 31, 32, 34 and 35 with `default: break`,
so for 33 the ammo total is never assigned and is still read at `0x39D50` and `0x39D98`.

Ammo is split `nAmmo = total / count` for every copy **except the last**, which gets
`total % count + total / count` — the whole remainder goes to the last soldier. `nIndex` is the object's
`uSortKey`, and **both hands are set to the same TactItem**.

Placement, ARM at `0x39C58` authoritative, with `k` the deployed counter at Map+0x15140:

```
nX     = 10 * Map_nCraftMapX + byte_A6688[4k + 16*(nMapLayers-1)]
nY     = 10 * Map_nCraftMapY + byte_A6688[4k + 16*(nMapLayers-1) + 1]
nLayer = aDeployCellOffsets[104*nMapLayers + 4k - 102]
```

**The table question, settled.** Both readers address `aDeployCellOffsets` the same way — `Tact_SelectTeamUnits`
at `0x39758` and this function at `0x39EDC` both compute `aDeployCellOffsets + 104*nMapLayers` and then read
`[R0,#-0x68]`, `[R0,#-0x67]`, `[R0,#-0x66]`. So the table is **104-byte records indexed by `nMapLayers - 1`,
each holding 26 four-byte entries**, and an entry is X cell offset at byte 0, Y offset at byte 1, layer at
byte 2, facing at byte 3. `Tact_SelectTeamUnits` is therefore already right; my note's "`entry[0]`,
`entry[1]`, `entry[2]`" meant entry *bytes*, not dword indices.

`aDeployPointCounts` (`byte_A67F0`) is `14, 12, 26` — points used per layer — and 26 is exactly one record's
capacity, so layer 3 uses every entry it has. It bounds the phase-3 scan only.

`aHwpsDeployOffsets` (`byte_A6688`) is a separate table: **twelve 4-byte records, 3 layers × 4 hardware deploy
points**, indexed `4*(nMapLayers-1) + k` with `k` the hardware-soldier index. Layer 1 gives
`(4,9) (4,11) (4,13) (0,0)`, layer 2 is **all zero** — hardware soldiers deploy exactly at the craft — and
layer 3 gives `(4,10) (4,12) (4,14) (4,16)`.

One residual risk worth naming before a port: `k` is the running count of hardware soldiers spawned across all
five object indices, and nothing caps it at 4. A fifth hardware soldier on layer 1 would read past the twelve
records into `aDeployCellOffsets`. Whether that can happen depends on the spawn counts in shipped
data, so it is worth checking rather than assuming.

### What porting settled

**The spawn count is `ObjectRec.uRecovScore` at 0xCA, not `uRecovCount` at 0xCC.** ARM at 0x39A24 is
`LDR R3, [R8,#0x108]` then `LDRH R11, [R3,#0xCA]`. Worth naming because the two fields sit next to each
other and the nearby `objbefore` save chunk also serialises 0xCA.

**The cell array index is `60 * (60 * nZ + nY) + nX`.** `Unit_PlaceOnMap @ 0x49a0c` builds it from a cell
pointer at 0x49A70: `LDRB [R0,#0x1C]`, `MUL #60`, `LDRB [R0,#0x1B]`, `ADD`, `MUL #60`, `LDRB [R0,#0x1A]`,
`ADD`. So the middle index is **nY** and the last is **nX**. The port's `MapCellLookup(nX, nY, nLayer)`
resolves exactly that, which is why `Tact_SelectTeamUnits` can pass its coordinates straight through.
It is also why `Map_PatchCraftSprites` looks swapped: the deploy and stamp tables feed the craft's Y into
the middle index, and the middle index is nY.

**Phase 5's scatter walk is bounded by `nTactItemCount`, not by the scatter table.** It stops early when
the source cell `(0,0,0)` runs dry, so the item count and the scatter count do not have to agree. The
non-base-defence branch is three cells in a column: nY = 55 with nX = 55, 45 and 35.

**The XCom base defence branch builds four 2 × 2 blocks per matching tile.** Read off the store sequence
at 0x3A0A4..0x3A184, which fills 16 pairs: X offsets {-1,0} then {3,4}, Y offsets {1,2} then {5,6}, in
blocks (-1,0)×(1,2), (-1,0)×(5,6), (3,4)×(1,2), (3,4)×(5,6). A tile matches when its byte in `unk_159D5C`
is 7, and the grid is 6 tile rows by 6 tile columns with the cell anchor at tile × 10 + 2 and tile × 10.
`MissionType_XcomBase` is 3 (ARM `CMP R2, #3` at 0x3A058).

## Proximity devices: `MapCell_TickProximityItems` (0x4b724) and `Combat_DetonateNearbyItems` (0x32fa8)

`MapCell_TickProximityItems` is decoded but not ported yet.

Both are ported in `src/pufo/game/pufo_combat.c`. Together they are the arming countdown and the
chain reaction.

**What a proximity device is.** A `TactItem` whose object has `nCategory == 3` and whose own `bUnk8` byte
(`TactItem + 0x08`) is set. `TactItem.bUnk7` at `+0x07` is the **arming countdown**: the tick function
decrements it once per call for every such item in the cell, and the device goes off when it reaches zero.

The countdown compare is a **signed char**: `v5 = bUnk7 - 1; bUnk7 = v5; v5 <= 0`. A device already sitting
at zero therefore becomes -1, fires again, and leaves 255 behind so it fires on every later call too.

**`MapCell_TickProximityItems` (516 B)** walks one cell's item list and switches on `ObjectRec.uSortKey`:

| sort key | behaviour |
|---|---|
| 14 | does **not** detonate. Sets `pCell->nFlags40 = (nFlags40 & ~0x78) \| 0x38`, removes the item |
| 15 | sets `Map.nFireOrigX/Y`, focuses when `pCell->bFlags44` bit 0 is set, resolves the shot, removes |
| 16 | `Map_ScanBlastFootprint` first — if it returns 0 the **whole function returns**, so the rest of the cell is not even ticked. Otherwise it sets the origin, focuses, resolves the shot, and returns **without removing the item** |
| other | sets the origin, removes the item, focuses, resolves the shot, returns |

So only key 14 is removed without detonating, and key 16 detonates but stays in the list.

Hex-Rays renders the shared shot tail at 0x4B900 as `pItemList->pObj->gap_0`, which reads as if it
reaches through the object. Case 15 settles it: `v8 = (int *)pItemList` and the argument is `*v8`, the
TactItem's own `pObj`. Both paths pass the object pointer. The default case removes the node before
that tail, but `MapCell_ListRemove @ 0x54b58` only relinks and never frees, so the pointer is still good.

**`Combat_DetonateNearbyItems` (416 B)** is a **9 × 9 box** `[nX-4, nX+4) × [nY-4, nY+4)` over all four
layers. Its gate *tests* `bUnk7 <= 0` but does **not** decrement it, unlike the tick function. Every candidate
is gated again by `Map_ScanBlastFootprint`, and after a successful detonation the walk **restarts from the
head of that cell's list**.

**Hex-Rays mangled both of these.** For the detonation box it rendered the bounds as `&result[-1].bUnk56` and
`&result->pWallSprA`, and the sort key as `result[3].nUnk20`. ARM at `0x32FC8` is plain: `LDRB R0,[R1,#0x1A]`
is `MapCell.nX`, `SUB`/`ADD #4` give the bounds, `0x1B` is `nY`, and the sort key is `ObjectRec + 0xC8`.
Reading the pseudocode here would have produced a nonsense loop range.

## `Tact_SpawnAliens @ 0x3a2ac` — decoded, blocked on data tables rather than functions

3,516 bytes. Every callee it needs is already ported: `Soldier_CreateAndClassify`, `Unit_bIsLarge`,
`Unit_ResetBattleState`, `Unit_RecalcInventoryStat`, `Unit_UpdateAnimPose`, `Map_PickRmpSpawnForUnit`,
`Font_LoadSpr`, `Font_BuildIndex`, `TactItem_ctor`, `Tact_KillUnit`. What is missing is the static data.

**The biggest correction: `byte_A6618`, `byte_A6628`, `byte_A6650` and `unk_A6658` are one block.**
`byte_A6628` is `byte_A6618 + 16`, and `byte_A6650` is `byte_A6618 + 56`. Verified byte for byte: the 40
bytes at 0xA6628 equal the 40 bytes at 0xA6618+16, and 0xA6650 holds `3, 5, 7, 4, 8`, which is
0xA6618[56..60]. The IDB currently presents them as four unrelated tables.

Read as one 80-byte block from 0xA6618:

| offset | bytes | how the original uses it |
|---|---|---|
| +0 | `1 0 0 3 5 0 7 10 0 13 15 0 20 0 0` | primary enemy count, index `3*(nTerrainVariant-1) + bUnk86290` |
| +64 | `0 0 0 0 0 0 0 0 0 1 1 0 2 0 0 0` | secondary count, same index, only when the lead race is not 10 or 12 |
| +56 | `3 5 7 4 8` | `bRankType` per race slot, index `v25` 0..4 |

`byte_A6628` is the **race composition** row: `v87[i] = byte_A6628[5 * v76 + i]` for i in 0..4, where `v76`
selects the alien set. `v76` comes from `gap_86301[0]`: 0 means `nTerrainVariant - 1`, 1 means 7, 2 means 6,
3 means 5, and a Mars-or-special mission overrides it to 5.

**The alien equipment list is eight u16 object indices at `off_AA008`, walked backwards.**
`0xAA008..0xAA017` is `18, 23, -1, 3, 6, 11, 12, 13`. The loop starts at `unk_AA016` (13) and steps down to
`off_AA008` (18), assigning to `apBelt[7]` down to `apBelt[0]`, while a parallel counter runs 14, 12, 10, 8,
6, 4, 2, 0 and **skips the entry where it equals 4** — which is exactly the `-1` sentinel at 0xAA00C. So the
sentinel is skipped by a counter, not by a test.

**`word_AA020` is eight mission-set totals**: `1, 35, 100, 300, 400, 350, 400, 300`, indexed by the same
`v76`. The race composition is then recomputed as `(word_AA020[v76] + unk_1472C8) / unk_AA030[i]`.

### There is no stack overrun at 0x3AB6C — the earlier reading was wrong

```
ADD  R6, SP, #0x48+var_20      ; 0x3AB50
MOV  R7, #8
...
SUB  R7, R7, #1                ; 0x3AB68
STR  R0, [R6]                  ; 0x3AB6C
ADD  R6, R6, #4
CMP  R7, #0
BHI  loc_3AB50
```

The composition array is **one 8-dword array** at `SP+0x70` (`bp-0x20` .. `bp-0x04`). The 5-entry seed
loop at `0x3A34C` writes its first five slots, this loop overwrites all eight, and the belt walk at
`0x3AD50` starts at `var_4` (`bp-0x04`) and steps **down** four bytes eight times over the same eight
slots. IDA's `var_14`, `var_10` and `var_4` are three elements of that one array that Hex-Rays had
declared as separate variables, which is what produced the claim that `v87` is `_DWORD[7]` and that the
eighth dword overruns a `TactItem` stub. It does not: the array is eight wide and both users agree.
In the IDB the three phantom `var_14` / `var_10` / `var_4` are deleted and the array head is named
`anComposition` at `bp-0x20`; IDA refuses a real 8-dword stack array over this frame, so the other
seven slots stay as unnamed frame bytes. The port uses one `i32 anComposition[8]`.

### Two stack slots Tact_SpawnAliens reads without ever writing

Both are in `docs/parity/bugs.tsv`. A C port cannot read uninitialised memory, so both come in as
`TactSpawnAliensEnv` fields.

- `var_24`, read at `0x3A8BC`. It is the fourth race group's count: the bound is
  `CMP R7, (var_24 + var_28 + var_2C + var_30)` and the group body at `0x3A8DC` loads
  `Map_abSpriteSet+3`. The whole function has exactly two references to `var_24` — the frame
  declaration and this load.
- `var_48`, the alien set index. Stored for mission mode 0 (`0x3A31C`), 1 (`0x3A30C`), 2 (`0x3A304`)
  and 3 (`0x3A2F4`, `R2` preloaded to 5), overwritten when `nMissionType` is 9 (`0x3A338`), and left
  unwritten by the `BNE loc_3A324` at `0x3A2F0` for any mode above 3. It then indexes the eight-row
  composition table and the eight-entry mission totals table.

### The rest of the shape

- Enemy counts by mission type: type 5 gives `15 - rand(5)` and `5 - rand(3)` plus `rand(5) + 5` reserves;
  type 3 gives `15 - rand(5)` and `3 - rand(3)`; types 4 and 9 give `20 - rand(5)` and `4 - rand(3)`;
  everything else reads the terrain tables. A lead race of 10 or 12 forces the secondary count to 0.
- The primary count is split into three race groups through a **string round trip**: `itos` then `stod`
  then multiply by the double pair `0x3FE99999 : 0x9999999A` (**0.8**) then `dtoi`. So group 1 is
  `floor(primary * 0.8)` clamped to at least 1, group 2 is `(primary - group1) / 2`, group 3 is the
  remainder. The earlier note here said 0.2 and called it a halving; both were wrong. Unpacking the
  constant pair gives 0.8, and `0x3F4CCCCD` is not referenced by this function at all.
- Race assignment is by position: the first `v84` soldiers get the lead race, the next `v85` get the second,
  the next `v86` get the third, and anything past that gets `rand(2) + 16`. A race value of 23 means
  "none" and falls through.
- Unit stats come from 32-byte records at `unk_A8F38 + 32 * nUnitType - 288`, names from 40-byte records at
  `0xB1140 + 40 * nUnitType`. Three of the stats get `+ rand(0x14) - 10`, so a jitter of -10..+9.
- A `-1` in record word 6 means "roll it": `auStat[6] = rand(0x32) + 30` and `auStat[7] = rand(0x32) + 30`
  with `bUnk422 = 1`; otherwise both are taken from the record and `bUnk422 = 0`.
- `bUnk435` is set on up to `v78` non-large aliens, excluding unit types 16 and 17. `v78` is
  `(Map_bUnk15115 - 20) * primary * 0.01` — the double pair `0x3F847AE1 : 0x47AE147B` is 0.01, so the
  divisor is 100 — and it is reduced by one when it would equal the whole enemy count.
- For the XCom base defence mission the soldiers go into `apHiddenSoldiers` and `nEnemyReserveCount` instead
  of `apEnemyUnits`, and they are **not** placed by `Map_PickRmpSpawnForUnit`.
- Unit types 18 and 21 are handed `apObjects[11]` with `nAmmo = -2`, which in a u8 field is 255.
- Finally: any enemy with `bUnk435` set is killed outright — `uHp = 0` then `Tact_KillUnit` — and the scan
  restarts from index 0 each time it kills one.

### A latent crash this port exposed in its own shot resolution

`Combat_ResolveShot @ 0x2f12c` clears the 41 x 41 x 4 hit window and writes `pOut->nDamage`
with no null test. The original reaches both through Map globals, so it always has them. The
port lifted them into parameters, and two port call sites pass NULL for both: the type-18 death
explosion in `pufo_unit.c` and this detonation. The proximity test crashed on the first call.

Fixed by giving `Combat_ResolveShot` a static scratch window and outcome when the caller passes
NULL, which reproduces the original's single shared window rather than inventing a new rule.
Callers that want to inspect the result still pass their own. This is a port-side defect, not an
original bug, so it is recorded here rather than in `bugs.tsv`.

## The sixth needed pass, and what it counts

Ported: `Base_UpdateAlienContainmentNeeded @ 0x65f04` (196 B), `Base_CountAmmoItems @ 0x656ac`
(116 B), `Base_CalcOccupiedSpace @ 0x655f8` (180 B). Coverage moved 662 -> 665 functions and
226,800 -> 227,292 bytes, which is exactly 180 + 116 + 196.

`Base_UpdateAlienContainmentNeeded` is the sixth member of the needed-flag family, and it is the
same machine as the other five: walk the 6 by 6 grid, and for every cell whose type byte is the
kind this pass cares about, write 1 into the needed grid while the budget lasts and 0 once it runs
out. Cells of any other kind are left exactly as they were. Kind 7 is alien containment, and the
budget is `ammo / 10 + 1`, or 0 when there is no ammo at all -- the `+ 1` is real, so five ammo
still claims one containment cell.

It also shows the family's odd habit a third time. ARM at 0x65F10 is `BL Base_CalcAlienContainmentCap`
followed by `MOV R0, R4`: the capacity is computed and then the return register is overwritten
before anything reads it. The capacity function is pure, so the port does not make the call. The
same discarded call is in `Base_UpdateWorkshopNeeded` and `Base_UpdatePsiLabNeeded`.

Its return value is loop-counter residue, always 6, reproduced.

### The two object-column readers

Both reach the object array through their first argument, and ARM shows that argument is the Map
base rather than the facility grid the pseudocode claims. At 0x656F0 the load is
`[R2 + 4*(i-1) + 4 + 0x108]`, and 0x108 is `Map.apObjects`, so the pointer is `apObjects[i]`. In
`Base_UpdateAlienContainmentNeeded`, ARM at 0x65F14 is `MOV R0, R4` before the call: R4 is the Map
base, and R0 had held the grid for the capacity call. Hex-Rays reused the grid for both.

The port takes the array and its count as parameters instead, which is how the rest of the family
already works.

What each one sums is the per-base quantity column `ObjectRec.auPerBaseVal[nBase]` at offset 0x2A:

- `Base_CountAmmoItems`: object indices 77 through 104 inclusive, plus object 114. The gate is
  `i > 76 && i < 105 || i == 114`.
- `Base_CalcOccupiedSpace`: soldiers whose `Soldier.nKind` matches the base, plus object 112 and
  object 113 only. Those two are tested with two compares against 0x70 and 0x71, not a range test.

The base index is a `char` in all three. `Soldier.nKind` is a `u8`, so a negative base index matches
no soldier, and the `auPerBaseVal` index is unguarded in the original: index -1 reads the two bytes
before the column. The port reproduces the narrowing and does not add a bounds test, since a bounds
test would change which objects contribute.

Both readers also carry a `!= 0` test on each quantity before adding it. Adding zero is a no-op, so
that test cannot change the total.

## MapView_DrawUnit ported

`MapView_DrawUnit @ 0x1c958` is 3,220 bytes and 126 basic blocks. It is the last leaf the iso layer
needs besides `MapView_DrawUnitFacingArrow`. It now lives in `src/pufo/game/pufo_isodraw.c` behind an
`IsoUnitDrawGate`, and `tests/test_isodraw.c` covers it in eight groups.

### What is now proven

**Visibility.** A unit draws when its own cell is visible. For a small unit that means
`pIso->pCell->bFlags44 & 1`. For a large unit it means any of a 2 by 2 block anchored on the unit's
cell: the cell itself, `nX + 1`, `nY + 1`, and `nX + 1` with `nY + 1`. The four loads are the Map cell
array at dword displacements 21710, 21711, 21771 and the array form, and 21711 - 21710 = 1 while
21771 - 21710 = 61, which is exactly one step in `nX` and one step in both.

**Height offset.** `pIso->nObjParam` (IsoDrawCtx + 0x54) is the minimum over that same 2 by 2 block of
the ceiling sprite's signed byte at `Mc1Rec + 0x24`, which the port already calls `iCeilParam`. It
starts at 0. The unit's own cell only contributes when the byte is negative; the three neighbours only
when the byte is strictly less than the running value. ARM at 0x1CDA8 is `LDRSB R3, [R0,#0x24]` for the
own cell and `LDRSB R0, [R0,#0x24]` with `CMP` and `STRLT` for the neighbours, so both reads are the
same field. Hex-Rays rendered the first as `pCeilSpr->gap_22[14]`, which is a different offset and is
wrong.

**Correction to an earlier note in this section.** `Soldier + 0x14` is `nAnimPose`, not `bFlags`.
`Soldier.bFlags` is at **Soldier + 0x04**, which the port already models and static-asserts, and ARM at
0x1D504 confirms it with `LDRB R3, [R0,#4]` and `ANDS R3, R3, #2`. The instruction at 0x1CC06 is
`LDRB R2, [R6,#0x14]`, which loads `nAnimPose`, and the `CMP R2, #2` after it tests that. The same
sequence re-confirms `nUnitType` at Soldier + 0xA0.

**The self-referential index is a direction table.** The pseudocode computes
`v20 = (char *)pIso + pIso->bAnimStep` and then reads `v20[101]`, which looks like a bug and is not.
`bAnimStep` is at IsoDrawCtx + 0x44 and 101 is 0x65, which is `bUnitFlag0`. So `v20[101]` is
`bUnitFlag[bAnimStep]`: the walk loop reads the direction byte for the step it is on.

That makes the large-unit branch a small, legible machine. The direction bytes are set once from how the
unit's own cell compares with the cell being drawn:

| unit cell vs drawn cell | flag0 | flag1 | flag2 | steps |
|---|---|---|---|---|
| same cell | 0 | 4 | - | 2 |
| same row, different column | 1 | 5 | - | 2 |
| same column, different row | 2 | 6 | - | 2 |
| different both | 3 | 7 | 8 | 3 |

and each step looks up `aUnitWalkGlyph[27 * nUnitType + 9 * bUnitFlag4 + direction]`, with a 4 pixel
lift when the direction is 8. `bUnitFlag4` itself is 2 when `nAnimPose` is 18, otherwise whether
`bFlags` bit 1 is set.

The compare order matters. `0x1CB48` tests `nX` first, so the "different column" row is decided before
the "different row" test at `0x1CB78` is ever reached. Two cells with equal coordinates but different
object identity therefore fall through to the diagonal set at `0x1CB90` -- the original never re-tests
identity after the first compare. That is unreachable through `MapView_bResolveIsoCell`, which resolves
`pIso->pCell` out of the same array, but the port reproduces the fall-through rather than tidying it.

**Which animation clock.** Unit types 20 and 21 read IDA's `nAnimPhase`. Every other type reads
`unk_1139EF`, which is Map + 0x37. ARM at 0x1CC08 is `CMP R0, #0x14`, `CMPNE R0, #0x15`, then
`LDRNE` of `unk_1139EF` and `LDREQ` of `nAnimPhase`. Hex-Rays renders the two the other way round.
Map + 0x37 is the same byte `UiModal_DrawDialog` uses to pick its horizontal band, so one byte serves
both jobs.

**The selection frame.** `unk_156928` is a PadImage. It is drawn only when `unk_157198->bQueueActive`
is clear, `pIso->pUnit == Map.pSelUnit`, and `pSelUnit->bFlags & 2` is clear; for a large unit it also
requires `pSelUnit->pCell == pIso->pCell`. Its position is

    x = nDrawX + ((32 - uWidth) >> 1)      or ((33 - uWidth) >> 1) when uWidth > 32
    y = aBobPhaseY[phase] - uHeight + nObjParam + nDrawY      plus 8 more for a large unit

and it is put on screen with `PadImage_BlitEx(pad, 0xFF)`.

**Palette is restored -- except on the two paths that fail the visibility gate.** The function saves
`pIso->nPal` on the stack at entry and zeroes it when the unit is operative, but the saved value is
written back only from `0x1D5DC` onward. The not-drawable exits jump straight to the POP: `0x1CA58` for
a large unit with no flagged cell in its 2 by 2 block, and the small-unit path at `0x1CAAC` after the
re-test at `0x1CA50`. So an operative unit behind an unflagged cell leaves `pIso->nPal` at 0 for the
caller, which reuses it for terrain. An earlier note here said every return path restores it; that was
wrong.

**Unoperative marker.** When the unit is not operative and `nPal > 7`, a glyph from the cursor group is
drawn at the unit position: base + 18 before the body and base + 17 after it. Both additionally require
`pCell->bFlags44 & 1` and (`Map.gap_86301[1] != 0` or `nPal != 255`).

**Glyph arithmetic is a triple table.** `word_A7230` holds three u16 per frame: a base, a phase scale,
and a facing scale. The pseudocode routes the product through `itos`, `muls` and `stoi`, which is how
Hex-Rays renders a soft-float multiply it cannot resolve. The real computation is
`nAnimParam = frame[0] + phase * frame[1] + facing * frame[2]`. The same chain appears twice, once for
the walk branch and once for the pose branch.

**Weapon icon column.** The weapon overlay takes its glyph from `ObjectRec.aItemQty[51]`, the same
out-of-range-looking column that `TactHud_DrawSelectedUnitPanel` reads at 52. Neither is a quantity.

### The context fields, now proven from ARM

`IsoDrawCtx` gained the walk state at these offsets: `nDrawMode` 0x0C, `nDrawAux` 0x10, `nSpriteX`
0x14, `nSpriteY` 0x18, `bAnimStep` 0x44, `nAnimFrame` 0x48, `nAnimParam` 0x4C, `bAnimKind` 0x50,
`bUnitFlag0..3` 0x65..0x68, `bUnitFlag4` 0x69, `nScratch2` 0x100 and the two-handed-weapon flag 0x12C.
The self-referential `(char *)pIso + pIso->bAnimStep` then `[101]` is `bUnitFlag[bAnimStep]`, as
recorded above -- not a bug.

### Where the port differs, and why

1. **The weapon overlay's null dereference at 0x1D384.** The original's object chain ends in
   `LDREQ R3,[R2]` with `R2 = pHandR = 0` when both hands are empty. The port skips the glyph. Whether
   that path is reachable is not proven: `Unit_DeriveAnimPose` derives `nAnimPose` from `bFlags` and
   the two hands, so poses 5..17 may imply a hand item, but nothing in the IDB proves it.
2. **`aUnitFacingDrawOff` is bounded.** The port refuses the read for unit types 14 and above rather
   than reproducing a read past a 224-byte table into the occupancy table at 0xA9488.
3. **`aUnitWalkGlyph` is bounded.** Type 22 has no walk row; the port yields glyph 0 (skip) instead of
   reading six bytes of type 21's tail and then into `aPoseFrameTable`.
4. **The group-base array is 28 entries, not 18.** `Tact_MapTypeToGlyphIdx` is not clamped anywhere --
   ARM at 0x1CE70 is `AND R3,R0,#0xFF` / `ADD R3,R5,R3,LSL#1` / `LDRH R3,[R3]` -- and it returns 18 for
   unit types 5 through 8 and `nType + 5` (up to 27) for anything the four-byte probe at Map+0x16..0x19
   misses. So the caller-supplied base block has to reach fontSys + 0x36. The documented bases stop at
   +0x32; +0x34 and +0x36 are the two words past them.

Everything else, including the `nPal` restore asymmetry and the zero-frame store, is reproduced.

### Table extents, settled against the image

Reading the bytes rather than the pseudocode closes most of it, and confirms **23 unit types,
indices 0 through 22**, independently of `g_aUnitMuzzleOffset`.

`aUnitWalkKind @ 0xA6B00` is 80 bytes per unit type, 23 types, 1840 bytes, which is exactly the gap to
`aUnitAnimFrame @ 0xA7230`. Each 5-entry group is a walk-kind permutation for one facing. Types 0 through 10 are
mostly `0,1,2,3,4`; type 11 is `3,0,2,1,4`; types 13 and 15 through 22 are `4,1,0,0,0`. So the five
animation slots are reordered per unit type, and several types share one reordering.

`aUnitWalkGlyph @ 0xA7408` is 27 bytes per unit type: three rows of 9, one per value of `bUnitFlag4`.
Types 0 through 21 have data. **Type 22 has none.** 27 * 22 = 594, and `aPoseFrameTable` starts at
0xA7660, six bytes past the end of type 21. So the earlier contradiction was not a wrong stride: the
walk table simply covers 22 of the 23 types, and a type 22 unit reaching the walk branch reads six
bytes of its own and then runs into the pose table. Whether that is reachable is a separate question
from what the table holds.

`aPoseFrameTable @ 0xA7660` is **760 bytes, 152 rows of 5**, and that is provable rather than
inferred. `aUnitTypePoseBase @ 0xA7958` is one byte per unit type holding a row base, and its values
are 19, 38, 57, 76, 95, 114 and 133, all multiples of 19. The largest pose is 0x11 = 17, so the
largest index is `5 * (133 + 17) + 4 = 754`, which fits 760 and does not fit anything smaller.

`aUnitHeightOff @ 0xA7A28` is 8 signed bytes per unit type, and the pattern is `1, 0, -2, 0` or
`1, 0, -1, 0` in the low four slots.

`aBobPhaseY @ 0xA7BE0` is exactly 8 signed bytes: `0, -1, -2, -3, -3, -2, -1, 0`. That is a bob cycle,
and the extent is not a guess because the very next bytes are the ASCII string `xcom_0`.

`aUnitWeaponYOff @ 0xA9310` is one byte per unit type, and only two entries are not zero: type 9 is 5
and type 11 is 255.

`aUnitFacingDrawOff @ 0xA93A8` is 16 signed bytes per unit type, two per facing, and the table is
**224 bytes: 14 unit types, not 23.** ARM at `0x1D280..0x1D294` is `LDRB R2,[R5,#5]` for `bFacing`,
`LDRB R3,[R5,#0xA0]` for `nUnitType`, `ADD R3,R2,R3,LSL#3`, `ADD R3,R0,R3,LSL#1`, then `LDRSB R3,[R3]`
and `LDRSB R3,[R3,#1]`: index `2 * (8 * nUnitType + bFacing) + axis`, sixteen bytes per type. The table
ends at `0xA9488`, and what follows is a different table - a u16 table `Map_TraceVoxelOccupancy (0x3242c)`
indexes as `4 * (8 * nUnitType + bFacing)`, which runs 736 bytes and ends exactly at
`g_aUnitMuzzleOffset @ 0xA9768`. The earlier note in this section, which found types 14 through 21
"alternating" and type 22 "starting with 80", was reading that other table as if it were the tail of
this one. A unit of type 14 or above that reaches the facing branch at `0x1D288` reads out of
`aUnitFacingDrawOff` and into the occupancy table.

## The nine iso tables are in the port

`tools/gen_isodraw_tables.py` dumps the nine tables out of `pufobeta.exe` and prints the C initialiser
block. That block now sits at the end of `src/pufo/game/pufo_isodraw.c`, the externs and the row counts
are in `pufo_isodraw.h`, and all nine are registered in `docs/parity/audit_tables.py`. The audit now
checks 18 tables and reports 0 mismatched. Coverage did not move - still 665 / 1103 functions and
227292 / 598792 bytes - because a data table is not a function.

Index shapes, each read off ARM inside `MapView_DrawUnit (0x1c958)` rather than off the pseudocode:

| Port symbol | IDB symbol | Address | Index | ARM |
|---|---|---|---|---|
| `g_aUnitWalkKind` | `aUnitWalkKind` | 0xA6B00 | `5 * (bFacing + 16 * nUnitType + 8 * bWalkHalf) + bAnimStep` | 0x1CF5C |
| `g_aUnitAnimFrame` | `aUnitAnimFrame` | 0xA7230 | `6 * nAnimFrame + 2 * k`, three u16 per frame | 0x1D048 |
| `g_aUnitWalkGlyph` | `aUnitWalkGlyph` | 0xA7408 | `27 * nUnitType + 9 * bUnitFlag4 + nDirection` | 0x1CBA8 |
| `g_aPoseFrameTable` | `aPoseFrameTable` | 0xA7660 | `5 * (nAnimParam + g_aUnitTypePoseBase[nUnitType]) + nWalkKind` | 0x1CFD8 |
| `g_aUnitTypePoseBase` | `aUnitTypePoseBase` | 0xA7958 | `nUnitType` | 0x1CFDC |
| `g_aUnitHeightOff` | `aUnitHeightOff` | 0xA7A28 | `8 * nUnitType + nAnimPhase` | 0x1D190 |
| `g_aBobPhaseY` | `aBobPhaseY` | 0xA7BE0 | the animation clock byte, Map + 0x37 | 0x1D54C |
| `g_aUnitWeaponYOff` | `aUnitWeaponYOff` | 0xA9310 | `nUnitType` | 0x1D3E8 |
| `g_aUnitFacingDrawOff` | `aUnitFacingDrawOff` | 0xA93A8 | `2 * (8 * nUnitType + bFacing) + axis` | 0x1D288 |

Two of the short tables are exact rather than merely bounded, and that is worth spelling out because it
is what makes the row counts safe to hard-code:

- `aUnitWalkKind` at 1840 bytes is exactly one more than the largest index the walk branch can form for
  type 22: `5 * (7 + 16 * 22 + 8) + 4 = 1839`. The table is 23 types wide because the index says so.
- `aUnitWalkGlyph` at 594 bytes is exactly one more than the largest index reachable for type 21:
  `27 * 21 + 9 * 2 + 8 = 593`, with `bUnitFlag4` in 0..2 and the direction byte in 0..8. Type 22 has no
  row at all, and a type 22 unit that reaches the walk branch reads into `aPoseFrameTable`. The port
  keeps the table at 22 rows rather than padding it to 23, so that read behaves as the original's does.

`aUnitAnimFrame` is 468 bytes of frames followed by four zero bytes before `aUnitWalkGlyph` at
`0xA7408`, and `aUnitWalkGlyph` has a six-byte zero tail before `aPoseFrameTable` at `0xA7660`. Neither
padding run is part of a table. `python tools/gen_isodraw_tables.py --check` prints the boundary bytes on
both sides of all nine, which is the check to rerun if an extent is ever in doubt.

## `Map_BuildMissionTerrain @ 0x22e10` — read end to end in ARM, ported

10,676 bytes, 313 basic blocks, 2,669 instructions, one caller (`Tact_InitMission @ 0x3c550`). Read in
full in ARM on 2026-10-13 and ported the same day into `src/pufo/game/pufo_terrain.c`. This section
records what the ARM settles; the port and the questions it closed are in the section after the last
one.

### The frame, and the grid that lives in the wrong place

Four `CDialogTemplate` objects are constructed on the stack (`pOut`, `var_D8`, `pTpl`, `var_118`) and
destroyed in that order at 0x25794..0x257C0. `Font_LoadTactSprites(pMap)` runs at 0x22E38, then
`memset(&pTpl.pDim1, 0, 0x24)`.

**That 36-byte memset is the whole 6×6 module grid, and its base is the address of the `pDim1` field.**
`CDialogTemplate` is 20 bytes with `pDim1` at +8, so the grid runs from +8 to +0x2B — past the end of
the struct, into the stack slots IDA names `var_97`, `var_96`, `var_95`, `var_90` and `var_78`. Every
access in the function is `base + 6 * tile + x`. A port that treats `pDim1` as a pointer, or sizes the
grid to the struct, is wrong.

There is a **second** 6×6 array, dword-strided, based at `var_90`: written at 0x24580 (zeroed per
entry), 0x24DA4 (`0, 1, 2, 3` into four slots) and 0x252A8 (`1, 2, 3, 0`). It is a per-tile kind code,
not the same thing as the byte grid.

### Phase 1 — the cell grid (0x22E6C .. 0x22F50)

`Map_nMapLayers` (Map+0x15114) == 0 jumps straight to 0x23DAC. Otherwise three nested loops build the
cell array:

| loop | bound | index step |
|---|---|---|
| outer, `nY` | `Map[Map+0x1511C] * 10` | +60 |
| middle, `nX` | `Map[Map+0x1511B] * 10` | +1 |
| inner, `nLayer` | fixed 4 | +`0x3840` bytes = +3600 dwords |

The ARM is `ADD R6, pMap, (60*nY + nX), LSL#2` then `STR R0, [R6, #0x15338]` with `ADD R6, R6, #0x3840`
per layer, so the slot address is `Map + 0x15338 + 4*(60*nY + nX) + 0x3840*nLayer` — a dword index of
`60*nY + nX + 3600*nLayer`. The cell array is **inline in the Map block**: 3600 cells per layer, 4
layers, 14400 dwords = `0xE100` bytes, spanning `Map+0x15338` to `Map+0x23438`, which is exactly where
`Map.apTactItems` starts. That adjacency is the proof of the extent. Per cell:
`operator new(0x3C)`; on success `MapCell_Reset()` then store the pointer; on failure store NULL and
**still call `MapCell_Init(NULL, nX, nY, nLayer)`**. Then `MapCell_Init(pCell, nX, nY, nLayer)`. The
14400 stride is the four-layer cell array the port already reaches through `MapCellLookup`, and the
`* 10` is `PUFO_CELLS_PER_TILE`.

The same three-loop allocation appears three more times, for the other mission paths: 0x23E74 (the
XCom base mission, `Map_nMissionType == 3`), 0x24640 (the `nMapLayers == 0` path, which first writes
6 into both `Map+0x1511B` and `Map+0x1511C`), and the grid is therefore always 4 layers.

### Phase 2 — which module goes on which tile (0x22F50 .. 0x23B08)

- `Map_nMissionType == 5` (Urban) gets two random passes at 0x22F60 and 0x22FC0: `Game_RandU16(2)`,
  and on zero either a random column of `3`s (`grid[rand + 6*i]`) or a random row rewritten `4`, or
  `2` where it was already `0`.
- `Map_nTerrainVariant` (Map+0x15111) non-zero: `Map[Map+0x15112] = (u8)Map[Map+0x1510C]` (a byte
  store from a dword load), then the module name is `9 * (3 * variant + Map[Map+0x1510C] - 3)` off
  `aTileSprIndex + 0xA8` — a nine-byte-per-entry name table with the **same index shape** as the alien
  race tables in `Tact_SpawnAliens`. `Map_LoadMa1(pOut, name, 4)`.
- The template's two dimension bytes are read through `_rt_sdiv(byte, 10)`: `nOffX = (u8)(byte1 / 10)`
  and `nOffY = (u8)(byte0 / 10)`, and the random bound is `Map[Map+0x1511B] - byte1/10`. This `/10`
  appears at every template in the function — the template header packs two tile counts in one byte.
- A nested walk over those extents writes `1` into the 6×6 grid wherever the template says a module
  sits.

### Phase 3 — `.rmp` spawn nodes (recurring, e.g. 0x2354C, 0x23ECC, 0x24FE8)

For each module tile: `CDialogTemplate_GetLevelCount`, `GetTemplateByte0`, `GetTemplateByte1`, both
divided by 10 into `nSizeY` / `nSizeX`, then `Map_LoadRmp(pRmpMissionData, nTileX, nTileY, nSizeX,
nSizeY, nLevel)`. The port already owns `Map_LoadRmp` and `RmpData`.

### Phase 4 — random ceiling breaks through `Combat_ResolveShot` (0x23B3C .. 0x23CAC)

`Map_nTerrainVariant == 2` sets the shot-count selector to 18 (`0x12`); `== 3` sets it to 51 (`0x33`);
any other variant sets it to 0 and skips the whole phase. In the 2/3 cases the shot count is
`Game_RandU16(0x1E)`, forced to 0 when `Map_bUnk15115 < 0x32`. The selector is not a count — it is a
dword-index offset into the Map block: the shot's object-array argument is
`*(Map + 0x108 + 4*selector)`, so variant 0/1 uses `Map+0x108` (the objects array), variant 2 uses
`Map+0x150`, variant 3 uses `Map+0x1D4`. The second shot loop always uses `Map+0x108`.

Then
`Game_RandU16(3)` twice, `n4 = 1 - r1`, `n5 = 1 - r2`, `if (n4 + n5 == 0) n4 = 1`, and a loop of
`n5 * 2` by `n4 * 2` cells that writes `Map+0x127A0C = x`, `Map+0x127A10 = y` and calls
`Combat_ResolveShot(pMap, x, y, apObjects[0], 0, 0)` — the same globals `MapView_DrawUnitHeightGlyph`
compares against. A second loop runs `(Map_bUnk15115 - 0x1E) / 10` shots at template-derived
coordinates. This is the original breaking ceiling tiles by firing at the map.

### Phase 5 — sprite stamping (0x24150 .. 0x256F8)

Four near-identical stamp blocks walk the module grid and write `Font_GetSprByIndex(pFont, index)`
into the four cell sprite slots at offsets `0x00`, `0x04`, `0x08`, `0x0C` — `pFloorSpr`, `pWallSprB`,
`pWallSprA`, `pCeilSpr` in the port's `MapCell`. The sprite index comes from `aTileSprIndex` as a u16
table (`LDRH`), `index - 1` is passed to `Font_GetSprByIndex`, and a zero entry means "store 0".
Guards read `aMissionTileTypes` at `0x159768 + 0x5F4` and `0x159768 + 0x5F0` and skip tiles whose byte
is `0x10`, `0x11`, `0x12` or `0x14`. Some branches write 0 into a *neighbouring* cell's slot through
offsets `0x15630`, `0x15CA4`, `0x15C00`, `0x15BB4`, `0x1582B8`, `0x18290` — alternate cell-array bases
that have to be resolved to cell coordinates before this is portable.

### Phase 6 — the tail (0x2571C .. 0x257C4)

A sweep over `4 * Map[0x1511B] * 10 * Map[0x1511C]` cells doing `cell->nFlags32 &= ~0x78` (bits 3, 4,
5 and 6 cleared) at every layer, then `Map_PatchCraftSprites(pMap)`, then the four
`~CDialogTemplate` destructors.

### The XcomBase path (0x23E08 .. 0x2412C)

`Map_nMissionType == 3` writes 6 into both `Map_nMapDimX` and `Map_nMapDimY`, runs the same three-loop
cell allocation, then walks `aMissionTileTypes @ 0x159D5C` in rows of 6. For each tile it builds the
name `<aTerrainNameByMissionType[nMissionType]><itoa(tileTypeByte)>`, loads it with `nFlag 2`, applies
it at `(nTileX, nTileY)`, and when `GetLevelCount <= 3` stamps, for levels 3 down to `nLevel`:
`Font_GetSprByIndex(2)` into cell slot `0x00` and `Font_GetSprByIndex(3)` into cell slot `0x0C`. The
cell index there is **not** the `60*y + x` form — it expands to

    3600*nLevel + 60*nTileY + 6*nTileRow + x

which is the 6×6-tile grid with each tile 6 cells apart inside a 60-cell row. A tile whose type byte is
`0x14` (20) skips its `Map_LoadRmp`.

### The name tables, verified from the image

Nine bytes per entry in all four:

| table | address | indexed by | contents |
|---|---|---|---|
| `aTerrainNameByMissionType` | `0xA67F8` | `Map_nMissionType` | jungle, culta, forest, xbase_, ubase_, urban, desert, mount, polar, mars |
| `aTerrainNameSuffix0` | `0xA7B20` | `Map_nMissionType` | `"0"` for every type except urban, which is `"4"` |
| `aPlane` | `0xA6668` | `Map_nMissionType - 1` | plane, lightnin, avenger |
| UFO module table | `0xA6590` | `3*(nTerrainVariant-1) + tier` | ufo1a, -, -, ufo_110, ufo_120, -, ufo_130, ufo_140, -, ufo_150, ufo_170, -, ufo_160 |

`tier` is `(u8)Map[Map+0x1510C]`, which the function also stores into `Map_bUnk15112` (`Map+0x15112`)
before using it. The UFO table ends exactly at `aPlane`.

### What is settled

- The cell index formula and the four-layer grid, in all four allocation sites.
- The 6×6 byte grid living at `&pTpl.pDim1` and overrunning the struct.
- The second dword-strided 6×6 kind array at `var_90`.
- The `_rt_sdiv(templateByte, 10)` convention for every template dimension.
- The module name table index `3 * nTerrainVariant + Map[Map+0x1510C] - 3`, nine bytes per entry.
- That the ceiling-break pass really calls `Combat_ResolveShot` and writes `Map+0x127A0C` /
  `Map+0x127A10` first.
- The final `nFlags32 &= ~0x78` sweep and the `Map_PatchCraftSprites` call.
- `byte_A6AF0` is a ten-entry per-mission-type module-count table, read as
  `Game_RandU16(byte_A6AF0[nMissionType])`: jungle 12, culta 19, forest 12, xbase 21, ubase 16, urban
  15, desert 12, mount 13, polar 14, mars 11. Urban is special-cased as `Game_RandU16(byte_A6AF5 - 3)
  + 3`, i.e. `rand(12) + 3`, so an urban module number is 3..14 while every other terrain starts at 0.
- `aMissionTileTypes @ 0x159D5C` is **runtime state, not a shipped table** — it lives in the `.bss` base
  block and has no bytes in the image. It is walked in rows of 6 by the XcomBase path.

### What is open, and blocks the port

1. The meaning of the 6×6 grid values `0, 1, 2, 3, 4` and of the `var_90` kind codes `0, 1, 2, 3`.
2. The alternate cell-array base offsets `0x15630`, `0x15BB4`, `0x15C00`, `0x15CA4`, `0x18290`,
   `0x182B8`, `0x15422C`, `0x1583B4`. Each is `Map + offset` and each has to be expressed as a cell
   coordinate before the stamping phase can be ported. `0x15630` is `Map_apCells0 + 190` dwords, so at
   least one of them is a cell index rather than a separate array — the stamping loops build their
   base as `Map + 4*(10*(rowAcc + col) + layerAcc)` and that arithmetic has to be reproduced exactly,
   not guessed from the offsets.
3. `aMissionTileTypes` sentinels `0x10`, `0x11`, `0x12`, `0x14`: `0x14` is proven to skip the tile's
   `Map_LoadRmp`; the other three are guards on the sprite stamping and their meaning is not settled.
5. `unk_128B00` (selects the `"0"` or `"15"` name suffix at 0x24A18) and the `word_128B04` /
   `word_128B08` pair (a random module position written into both grids and used as the `Map_LoadRmp`
   tile).
6. Whether the `nMapLayers == 0` path (0x23DAC) and the `Map_nMissionType == 3` path (0x23E08) share
   the module phases by fallthrough or by separate copies — the function has three near-duplicate
   copies of the same stamping block and they are not byte-identical.
7. The allocation-failure behaviour: the original stores NULL and then calls `MapCell_Init(NULL, ...)`.
   Decide reproduce-or-deviate before writing it.

## `Map_BuildMissionTerrain` — the `nMapLayers == 0` path and the tail, decode finished

Read 2026-10-13. This closes the last structural question: the `nMapLayers == 0` path and the
`Map_nMissionType == 3` path are **separate copies**, not a shared fallthrough. They build the same
kind of layout with different constants and different sprite indices, and the stamping blocks differ.

### The empty-layers path (0x23E08 .. 0x256F8)

`Map_nMapLayers == 0` jumps to 0x23DAC, which then splits on mission type. The non-XcomBase arm:

1. Writes 6 into `Map_nMapDimX` and `Map_nMapDimY`, zeroes the 6×6 dword kind array (`36` dwords,
   `sp+0x94`), and runs the same three-loop cell allocation.
2. Builds `<aTerrainNameByMissionType[nMissionType]>` + (`unk_128B00 ? "15" : "0"`), loads it with
   `nFlag 2`, and applies it at tiles (5,5), (4,5), (3,5).
3. Sets the module grid cells `pTpl_8[33]`, `pTpl_8[34]`, `pTpl_8[35]` to 1 — the last row, columns
   3, 4 and 5.
4. Stamps `Font_GetSprByIndex(0x3F)` into cell slot `0x00` and `Font_GetSprByIndex(0x3E)` into slot
   `0x0C`, for levels 3 down to `nLevel`, at three columns 10 cells apart.
5. Loads three `.rmp` sets, for tiles (5,5), (4,5), (3,5).
6. `word_128B04 = Game_RandU16(5)`, `word_128B08 = Game_RandU16(4)` — a random tile in the 6×6 grid —
   applies the same template there, stamps it, and writes 1 into the module grid at
   `6*y + x`, `6*y + x + 1`, `6*(y+1) + x`, `6*(y+1) + x + 1`. The kind array gets `0, 1, 2, 3` at
   those same four positions.
7. Fills every remaining empty module cell with a random module, with this filter on the module
   number drawn from `byte_A6AF0[nMissionType]`:

       while (n == 4);
       if (n != 0 && n != 15 && (n != 12 || (x != 0 && x != nDimX-1 && y != 0 && y != nDimY-1)))

   So module numbers 0, 4 and 15 are never used at all, and 12 is never used on the border. If the
   chosen module would overflow the grid, the cell is retried.
8. Two final stamping passes keyed on the kind array: one skips kind 3 and kind 1, the other skips
   kind 2 and kind 3.

### The tail (0x2571C .. 0x257C4)

`cell->nFlags32 &= 0xFFFFFF87` (bits 3, 4, 5, 6 cleared) over `10 * nMapDimY` rows, `10 * nMapDimX`
columns and four layers, then `Map_PatchCraftSprites(pMap)`, then the four `~CDialogTemplate`
destructors in the order `v266`, `pTpl`, `v273`, `pOut`.

### What that closes

- Open question 6 is answered: separate copies, not fallthrough.
- `unk_128B00` is the `"0"` / `"15"` name-suffix selector, and `word_128B04` / `word_128B08` are the
  random 6×6 tile position (`rand(5)`, `rand(4)`) that both grids and the `.rmp` load agree on.
- The module-number filter above is a hard rule, not a guess — it is written out in the ARM.

## `Map_BuildMissionTerrain` ported, and what the port settled

Ported 2026-10-13 into `src/pufo/game/pufo_terrain.c` as `Map_BuildMissionTerrain(MapTerrainEnv *)`,
with `tests/test_terrain.c` (59 checks). Coverage moved 669 → 670 functions and 239,932 → 250,608
bytes, exactly the function's 10,676.

### The env struct replaces the globals the original reaches through

The builder touches the `Map` block at eleven offsets and the font block at one. All of them are
caller-owned in the port: the 14400-slot cell array, the Map dimension/type/variant/tier bytes, the
shot origin pair, the random spawn tile, `aMissionTileTypes`, the shot-object array at `Map+0x108`,
and the `.rmp` scratch. Two hooks stand for things the builder cannot own: `Font_LoadTactSprites`
(0x257c4, not ported) and `Combat_ResolveShot @ 0x2f12c`, which IS ported but whose `CombatCtx`
(`TactState`, the three rosters, the 41×41 hit windows) belongs to whoever owns the tactical context.

### The cell array is the port's to allocate

The builder mallocs the 14400 `MapCell` objects and fills a caller-owned `MapCell *[14400]`, including
the allocation-failure path: it stores NULL and still calls `MapCell_Init(NULL, x, y, layer)`, which
has no null guard in the original either. The tail's flag clear has no null test either. Both are in
`bugs.tsv` and both are reproduced rather than papered over.

### The tail mask is weaker than it looks

`nFlags32 &= 0xFFFFFF87` clears bits 3..6 and KEEPS bits 0, 1, 2 and 7. `MapCell_Init` already masks
with `0xFFFFFF80`, which clears bits 0..6. So on a grid the builder just allocated, the tail is a
no-op; it only matters for a cell whose flags were set between allocation and the tail. An earlier
note in the IDB described it as "clears the low 7 bits and bit 7", which is wrong in both directions.

### The two kind-array passes read `kind[row][col + 1]`, not `kind[row][col]`

This is the one thing the pseudocode made unobvious. Pass A walks the 6×6 dword array from its start
and its guard is `LDR R0, [R3, #4]` at 0x25338. Pass B starts one dword into the array (`&v277`) and
reads `[R3, #0]`. Both therefore test the same element: the kind one column to the right. Pass A
walks columns `0 .. nDimX-2`, pass B walks `0 .. nDimX-1` and its rows stop at `nDimY-2`, so neither
reads past the 36 entries.

### The two XCom-base guards look at different neighbours

Pass 1's second guard is `MOV LR,#0x5F0 / ORR LR,#5` → `aMissionTileTypes[6*tileRow + col + 1]`, the
tile one COLUMN right (0x24194). Pass 2's is `ADD R3, R8, #0xFF` → `aMissionTileTypes[6*(tileRow+1) +
col]`, the tile one ROW below (0x24394). Each pass also has its own sentinel set: pass 1 skips
`0x10`, `0x12` and `0x14`; pass 2 skips `0x10`, `0x11` and `0x14`. `test_terrain.c` pins all of it,
including the neighbour direction in both senses.

`aMissionTileTypes` is reached as `0x159768 + 6*tileRow + 0x5F4`, so its row stride is six BYTES and
the block base at `0x159768` is 0x5F4 bytes before it.

### The craft pad tile and `Map_nCraftMapX/Y` are the same two bytes

The plane placement writes `Map+0x15139` / `Map+0x1513A`, which IDA already names `Map_nCraftMapX` /
`Map_nCraftMapY`. `Map_PatchCraftSprites @ 0x25ab4` reads exactly those two bytes (its literal pool
at 0x25E64 / 0x25E68 is `0x15139` / `0x1513A`). So the builder writes the craft's tile and the tail
immediately consumes it — one env field, not two. `CraftSpritePatchEnv`'s comment in `pufo_font.h`
said `0x15199` / `0x1519A`; that was wrong and is corrected.

The flag that selects the pad's sprite group is also the same value twice over: a template's slots
resolve through `*(u16 *)(fontSys + 2 * nFlag)`, the pad is loaded with `nFlag 3`, and
`Map_PatchCraftSprites` reads the u16 at `fontSys + 0x06`. The craft pad and the craft sprites come
from one sprite group.

### The fallback always appends the literal "4"

The `nMapLayers == 0` non-XcomBase path builds `<terrain name>` + `pDefault`, and `pDefault @ 0xAAD4C`
is the two-byte string `"4"`. It is NOT the per-mission-type `aTerrainNameSuffix0` entry — so even a
jungle fallback loads `jungle4.ma1`. The other fallback name is `<terrain name>` + (`Map_bSpawnAlt ?
"15" : "0"`).

### The shot sweep, and what its selector indexes

Variant 2 selects 18 and variant 3 selects 51; anything else selects 0 and the sweep is skipped. The
shot's object argument is `*(Map + 0x108 + 4*selector)`, so 18 is `Map+0x150` and 51 is `Map+0x1D4` —
entries of the object-pointer array that starts at `Map+0x108`, not a mode value. The count is
`Game_RandU16(0x1E)`, forced to 0 when `Map_bUnk15115 < 0x32`. The two `Game_RandU16(3)` draws that
produce the X and Y steps happen **whether or not** the count survives that gate, so they are not
inside the `if`. The second, randomised sweep runs `(Map_bUnk15115 - 30) / 10` shots and always uses
object slot 0.

### Deviations the port makes on purpose

- The original reuses the interior bytes of its four `CDialogTemplate` objects as scratch (the module
  name at `pOut + 8`, the `itoa` buffer at `v273 + 0xC`). The port uses its own buffers.
- The cell lookup bounds-checks. The original indexes the 14400-dword array with no test at all.
- `aPlane[9*(nMissionType-1)]` with mission type 0 and `aUfo1a[3*(variant-1)+tier]` past slot 14 both
  read out of the table in the original. The port yields an empty name, which fails the `.ma1` load
  the way a missing member does. Both are in `bugs.tsv`.

### Still open

The meaning of the `aMissionTileTypes` sentinels `0x10`, `0x11` and `0x12` is not settled — only that
they suppress the stamping passes, and that `0x14` additionally suppresses that tile's `.rmp` load.

## `Tact_InitMission @ 0x3c550` — ported, and the mission-start contract it fixes

`src/pufo/game/pufo_tactinit.{h,c}`, asserted by `tests/test_tactinit.c` (8 cases). 4,120 bytes cited,
which is exactly `0x1018`, the function size in `docs/idb/inventory-grouped.tsv`.

### The prologue flag block, in the ARM's order

`0x3C64C..0x3C734` is a flat run of stores with no structure, and the order is the ARM's, not a
sorted one. Two things in it are easy to get wrong:

- `Map+0x00` is set to **1**. `0x3C678` is `MOV R0,#1` followed by `STRB`, so the tactical flag is
  raised here, not cleared. `Map_InitFacingScratch` at `0x3CB30` then reads it as the tactical
  argument.
- `Map_nUiScreen @ Map+0x3A` is cleared with `STRH` at `0x3C67C`. It is a **u16**, and it is cleared
  here — which is why the briefing's save of it (below) always saves zero.

`var_s28` is `nMode`, stored once at `0x3C560` and re-read at `0x3CBB8` (the mode split) and `0x3D298`
(the briefing guard). The port keeps it as the `nMode` parameter.

### The deploy-point sweep reads the craft tile the terrain builder just re-rolled

`0x3CC20` is the sweep. Its record address is built as `4*n + 104*(Map_nMapLayers-1)` bytes into
`aDeployCellOffsets @ 0xA66B8`, so the table is 26 four-byte records per 104-byte group and the group
index is `nMapLayers - 1`. Record bytes 0, 1 and 2 are `dx`, `dy` and `layer`, loaded unsigned, and the
cell it names is

```
3600*entry[2] + 60*(10*Map_nCraftMapY + entry[1]) + (10*Map_nCraftMapX + entry[0])
```

The two craft bytes are `Map_nCraftMapX/Y @ 0x128AF1 / 0x128AF2` (`Map+0x15139 / 0x1513A`) — the same
two bytes `Map_BuildMissionTerrain` writes when it picks the plane tile. So the sweep does **not** use
the caller's craft position; it uses whatever the terrain pass left. A test that pins the craft tile in
the fixture and then asserts the stamped cells is wrong, and it was wrong here until this step caught
it. The assertion now reads the position back after the run.

`Tact_SelectTeamUnits` builds its anchor from the same table and the same two bytes, which is the
cross-check that the stride is right.

### The layer switch has a hole, and the showship reveal falls into it

`0x3CE70` switches on `Map_nMapLayers`: 1 and 3 store `(5,10)` into `var_350`/`var_354`, 2 stores
`(5,5)`, and there is no default. A layer count of 0 skips the consumer, so the hole is only reachable
at 4 or more — and the consumer is the showship reveal centre at `0x3CEA4`, which reads both. Recorded
in `bugs.tsv`; the port initialises both to zero.

The reveal header order is the other easy mistake: word 0 is the **entry count** and drives the outer
loop, word 1 is the **entry length** and drives the inner one, and `0xFF` means this entry reveals
nothing. The centre is `10*craft - half-extent + (var_350, var_354)`, and every revealed column is run
over all four layers — the `+0x3840` step in the ARM is 3600 dwords, one layer.

`AssetReader_Find` for `showship.dat` at `0x3CADC` is not tested. A missing member leaves the reader
size at zero, the code allocates zero bytes, and the reveal block reads its two header words through
that allocation. The port stores NULL for a zero-size member, which the reveal already treats as
"nothing to reveal".

### The briefing, and the value it saves

`0x3D29C` is `LDRH R0,[Map_nUiScreen]` then `STR R0,[Map+0x8C]` — `apUiSlotsA[11]` gets the screen
field as a dword — and `0x3D2B0` sets the field to 20 with `STRH`. Because the prologue cleared the
field, the saved value is always 0 on this path. A test that sets the field before the run and expects
to find it in slot 11 is testing the fixture, not the game.

The tail at `0x3D434` is `nLineCount += 2`, and `UiPanel_WrapText @ 0x37144` ends with
`pPanel->nLineCount = nLineStart + committed`. So a one-line wrap from line 0 leaves 1, and the
briefing panel finishes at 3, not 2.

Two original defects sit in this block. When `operator new(0x14)` fails, `0x3D2CC` stores `R11` — the
zero the prologue set at `0x3C598`, never a pad pointer — into the pad global and calls
`PadImage_Load` anyway; `0x3D2E8` then writes through the stored NULL. And the pad global `unk_15692C`
is overwritten without freeing what the previous mission allocated, both on the skip path at `0x3D548`
and on the run path at `0x3D2D0`.

### Deviations the port makes on purpose

- The three singletons (`TactState`, `TactCtx272`, `TactBuf11408`) and the five panels are caller-owned
  in the port; the original allocates them into globals.
- `Tact_LoadPalettes` is called with `*pEnv->pnGamma`. In ARM it takes no argument at all —
  `Tact_BuildPaletteLuts` reads `Map_nGamma @ 0x128AB8` itself. The argument is residue in the
  original and harmless; the port keeps its existing signature.
- The briefing pad is dropped rather than freed, matching `0x3D548`.
- `pufo_ui` calls (`UiPanel_ctor`, `UiPanel_SetBytes6to11`, `UiPanel_WrapText`, `Tact_LoadUiPads`,
  `Tact_InitUiControls`) and the audio calls go through hooks so `pufo_game` does not link `pufo_ui`.

### What this step found in the fixture rather than in the port

`Tact_BuildVisRayTable` writes `PUFO_VIS_TABLE_BYTES` = 14,880 step bytes. The new fixture sized its
step array at 256, so the builder overran into the asset registry's statics and the next
`AssetReader_Find` dereferenced a clobbered directory pointer. The ray table itself was correct; the
buffer was not. Worth remembering because the symptom was a crash three calls away from the cause.

## `BaseFac_SetCellBuildState @ 0x623bc` and `Base_GrantStarterItems @ 0x95fa4` — ported

`src/pufo/game/pufo_geoscape.c`, asserted by `tests/test_geoscape.c` (37 new checks). 336 + 124 = 460
bytes cited. These are the first two links of the chain that now stands between the port and
`LoadGame`: `LoadGame` → `Geoscape_InitUI` → `Game_StartNew` → these two.

### What the grid actually is

The original's own type names settle it and the ARM's arithmetic confirms it. `gBaseFacGrid @ 0x159768`
is 1524 bytes: `buildDays u8[8][6][6]` at +0x000, then `fac BaseFacBlock[8]` at +0x174, each block 144
bytes of `tileA`, `tileB`, `layer2`, `facType`, each a 6 by 6 layer.

`BaseFac_SetCellBuildState` forms two row bases and every store goes through one of them:

```
fac row    6 * (24*nBaseIndex + nY) + nX   == 144*nBaseIndex + 6*nY + nX
days row   6 * (6*nBaseIndex  + nY) + nX   ==  36*nBaseIndex + 6*nY + nX
```

The ARM multiplies `nY`. That is the coordinate the other readers of this grid call the column, so the
port's existing `6*column + row` and this function's `6*nY + nX` are the same indexing seen from two
ends.

### The hangar is the 2 by 2 case, and it is asymmetric

Facility type 11 is the hangar — the kind `Base_CalcHangarCap` counts at 0x6611c. It writes four
tileA quadrants, 9 at (nY,nX), 10 at (nY,nX+1), 11 at (nY+1,nX) and 12 at the diagonal, and it writes
no `tileB` at all. Its build days go to three cells: the ARM stores them at +0, +1 and +7 bytes from
the anchor, and +7 is `6*(row+1) + (col+1)`, the diagonal. The (nY+1,nX) cell never gets a build-day
byte even though its tileA quadrant is marked.

Then the shared tail writes `facType` in the anchor cell only. That is why a hangar counts once in
`Base_CalcHangarCap` and not four times, despite marking four tileA cells.

### `aFacBuildSpec`

`0xAA8F8`, 18 records of three bytes: the tileA byte, the tileB byte, and the build days read with
`LDRSB` at 0x623EC so it is signed. Indexed `3*nFacType`. IDA declares it 56 bytes; the real extent is
54 and the last two are zero padding. Record 11 is `FF, FF, 19` and its two tile bytes are never used,
because type 11 takes the hangar path — which is a useful confirmation that the special case really is
keyed on the type and not on the spec entry.

### What the port refuses

The original bounds nothing: `nBaseIndex`, `nX`, `nY` and `nFacType` all go straight into address
arithmetic, and a type of 18 or more reads past `aFacBuildSpec`. The port refuses out-of-range indices
and returns 0. It also uses the grid argument, which the original accepts and never reads — both
callers pass the grid they mean, so the difference is latent rather than behavioural.

The return value `6*(24*nBaseIndex + nY + 18)` is the address delta the `facType` store used. It is
reproduced unchanged, because no caller uses it and inventing a status would be a guess.

## `Ship_SpawnAtSlot @ 0x2cb14` — ported, and the hangar scan has three quirks

`src/pufo/game/pufo_ship.c` behind `ShipSpawnEnv`, asserted by `tests/test_ship.c` (19 new checks).
664 bytes cited.

The first half is ordinary: `new(0x44)`, `Ship_Ctor` with the **current** value of the next-craft id,
store at `apCraft[nCraftCount]` (Map+0x10AC, count byte at Map+0x1194), a `StrategList_AddItem` of
type 1 at the home base's own x/y read from `BaseRec+8` and `BaseRec+12`, and only then bump the id
and the count. The store uses the count before the bump, which is why the scan's later re-read through
`Map+0x10A8 + 4*count_after` lands on the same slot — the two offsets differ by exactly one element.

The second half searches the home base's facility grid for a hangar cell to attach the craft to:

```
fac[nBase].facType[v10][result] == 11
fac[nBase].layer2[1][result+1]  == 0
buildDays[nBase][v10][result]   == 0
```

and on the first match writes `Ship.bHangarX = result+1`, `Ship.bHangarY = v10+1`, and the craft glyph
33..37 (for types 0..4) at `fac[nBase] + 6*v10 + result + 79`, which is `layer2[v10+1][result+1]`.

Three things about it are the original's, not the port's:

- **The `layer2` test is pinned to row 1.** The ARM forms the test address as `144*nBase + result +
  451` with no `v10` term, so it always reads `layer2[1][result+1]`, while the stamp it authorises
  goes to row `v10+1`. For any row below the first, the tested cell and the written cell are different
  cells. A hangar at `facType[3][0]` is therefore refused by whatever happens to be in
  `layer2[1][1]`, not by `layer2[4][1]`.
- **The scan is 5×5 of a 6×6 grid.** Both counters stop at 5, so a hangar cell in the last row or the
  last column is never found.
- **An unknown craft type writes the coordinates and stamps nothing.** The switch is five `CMP`/`BEQ`
  pairs with no default, so type 5 falls into the inner loop's increment. The hangar coordinates were
  already stored at that point and stay stored, the loop carries on, and the function returns 5.

A failed `new(0x44)` still stores NULL into `apCraft` and still bumps both counters; if the scan then
matches, the coordinate write goes through that NULL. The port skips the write in that case.

The port's own first attempt at this got the stamp offset wrong — it used `6*(v10+1) + (result+1)`
without the 72 and wrote into `tileA`. The test caught it, which is the reason the offset is written
out in the source comment rather than folded into a helper.

## The start menu is clickable: `Input_OnPointer @ 0x13c6c` + `Input_OnPointerUp @ 0x148d0` on screen 17

Both functions were already ported in `src/pufo/game/pufo_input.c`; what landed now is the shell side
(`src/pufo/app/pufo_app.c`): `WM_MOUSEMOVE` / `WM_LBUTTONDOWN` / `WM_LBUTTONUP` map the window
coordinates back through the letterbox inverse (`App_MapPointer`, the exact inverse of
`Gx_Present`'s scale-and-center) into logical 240x320 coordinates and call the two handlers with a
`TactPointerWorld` whose `pBank` is the same `UiWidgetBank` the menu controls live in
(widgets 113..127 = `0xAD4F0`), so `Input_OnPointerUp` reaches the controls `GameUI_InitStartup`
installed the handlers into.

The screen-17 mechanism, re-verified in ARM:

- **Press** (`0x13c6c`): first clears all seven `MainMenu_abRowDisabled` bytes at `0x156AB0`, then
  marks the rows whose box contains the pointer. Mode 0/1: four rows, x∈[50,190], y∈[115+22i, …].
  Mode 2: seven rows, x∈[24,216], y∈[65+22i..83+22i], plus `UiControl_HitTest` on the standalone
  start control at `0x1571B4`. A mark does two things: `MainMenu_Draw` forces that row's blend key
  to 255 (the bright row), and the release fires that row's `pfnClick`.
- **Release** (`0x148d0`): runs the release half of `UiControl_HitTest` (x=0, y=0, bDown=0) over the
  menu controls, then fires `pfnClick` of the first marked row and clears the marks. In mode 0 the
  clear only covers the rows **before** the hit row; the hit row itself is cleared only when
  `unk_188D4C` (row-sticky) is 0 — the port passes that byte through `TactPointerWorld`.

Verified visually with a new debug flag: `pocketufo --dump-frame out.ppm --click 120 148` performs one
press+release at logical coordinates before the dump. The click lands on mode-0 row 1 (y 137..159),
whose handler is `MenuCtl1_OnClick @ 0x1a034` — sample 16, `SaveSlot_FormatList`, list mode 2 — and the
dumped frame shows the seven-slot load list (all seven rows labelled from text record 1074, "no
saves", because the run directory has no `.sav` files) with the "Отмена" back control (record 1083)
at y 230. The menu click path is therefore live end to end: pointer → hover mark → release →
handler → layout change → next frame.

## The credits screen opens: `Titles_Init @ 0x380d8` and `Titles_ctor @ 0x46e88`

Both ported in `src/pufo/game/pufo_titles.c` (24 new checks in `tests/test_titles.c`), 688 bytes
cited. Everything they set up was already ported (`Titles_LoadScene`, the star/particle animation,
`Titles_DrawAnimatedFrame`, `Titles_DrawCreditLines`); what was missing was the opener.

`Titles_ctor` is smaller than the state block suggests: it zeroes the scene index (state +16),
creates the titles surface — `new(0x28)`, `SoftSurf_ctor`, `AllocPixels(240, 320, mode 0)` — and
seeds 190 stars (state +87220) with `x = RandU16(0xF0)`, `y = RandU16(0x140) - 320`, `speed =
RandU16(3) + 1`, `amplitude = RandU16(0x64) + 1`, then runs one `Titles_UpdateStars`. The mode is
the fact that matters: **mode 0 is one byte per pixel**, so the 240x320 surface is 0x12C00 bytes —
which is exactly the size of the `memset` at the end of `Titles_Init`, and why
`Titles_PresentToSoftSurf` can blit the surface as an 8-bit indexed glyph through palette 12. The
seeded `y` range is −320..−1: every star starts above the top edge and walks in.

`Titles_Init` runs the whole opener in one order: ctor the 0x154C0 state block, 44 credit rows from
the image blob `unk_A63A8` (records 0x587..0x59C, then 0x655/0x656, a 0x5A0 spacer, 0x64A..0x652,
spacer, 0x653/0x654, spacer, 0x59D..0x5A2 — the spacer record recurs four times as a blank line),
font re-init with the glyph base taken from the running frame count **before** `Font_LoadSpr("font")`,
`fade.pal` into slot 12, screen id 16, scene 0, `StartTimers_C`, zero the credits scroll (state
+87228), `titles.pal` into slot 31, load the five `titles0..4.pad` glyph blobs into `Titles_apGlyphs`
(renamed from unk_1571A0 — the table `Titles_DrawCreditLines` indexes for '@' records), the "tact"
samples, music track 3, both volumes, and the surface memset.

Two facts the pseudocode left implicit:

- **The credits scroll is state +87228, not `nTitlesScroll` (unk_138E88).** `Titles_DrawCreditLines`
  reads it through the state pointer it is handed, and the WndProc 0xC352 branch for screen 0x10
  advances it by one per tick, wrapping past `20 * (nTitlesMaxRows + 48)` (0xA0C58). The port
  aliases it: `Titles_DrawAnimatedFrame` copies `nScroll` into the credit-lines env before the call.
- **`unk_156930` is write-only residue.** Cleared here and in `Map_ComputeReachable`, never read
  anywhere in the binary; the port does not model it.

Verified visually: `--dump-frame out.ppm --click 120 170` clicks mode-0 row 2 (`MenuCtl2_OnClick @
0x1a064`: shutdown, then `Titles_Init`), and the dumped frame after 40 anim ticks shows the first
credit line ("Kuznetsov Mikhail", record 0x587) at y 300 with the stars' first dots entering at the
top edge. IDB: comments at 0x380d8 / 0x46e88 / 0x1571A0 / 0x156930, rename unk_1571A0 →
Titles_apGlyphs.

## The load driver opens: `LoadGame @ 0x44bbc` (3,400 B) ported with hook seams

`LoadGame` is the mirror of `SaveGame` for `sav<n>.sav` and the keystone of a cold start. The whole
driver now lives in `src/pufo/game/pufo_save.c` as `LoadGame(DatFile*, LoadGameEnv*)` with a
`LoadGameHooks` seam for the callees whose environments belong to other subsystems. `test_save`
verifies it with a sav7 round-trip: `SaveGame` writes a geoscape slot (one base, one craft, one
strateg item, one soldier, time = 1 year), then `LoadGame` reads it back and every re-derivation
pass is checked.

Two long-standing IDB errors died in this step, both caught by reading the ARM instead of the
pseudocode:

- **The tactver value is NOT `apObjects[4]`.** `0x44C94` is `STR R3,[R7,#0x118]` where R7 is the
  first argument -- and every caller (`SaveLoad_Slot0..6`, `MenuCtl8..14_OnClick`) passes
  `&MainMenu_pSlotCtx @ 0x156938`, the 7x40-byte save/load line buffer. So the dword lands at
  0x156A50, right after the lines, and nothing in the binary ever reads it (the only 0x118 access
  on that base is the store). It is write-only residue, like `unk_156930`. The object table is
  `Map+0x108` (`unk_113AC0`), a different block -- the earlier "stores the value 1 as a
  live-looking pointer into the object table" reading was wrong, and the port hands the value out
  through `pnTactVerOut` instead of corrupting the table.
- **The "shot object" is `apObjects[0]` itself.** The word-zeroing pass at 0x45750 reads
  `[Map+0x108]` -- the object table's first entry -- and zeroes `nCraftCount` u16 words starting
  at byte 58 (word 29, `aItemQty[0]`), then byte 0xCA (word 101) even when the craft count is zero
  (the loop gate is a `BLS` that jumps straight to the 0xCA store). There is no separate global at
  Map+0x1108.

The teardown truth table, re-verified in ARM at 0x44D08..0x44D84: `bTactFlag && !bGameState` ->
`Tact_Shutdown(0)` + `Geoscape_InitUI(1)`; `!bGeoscapeUi` -> `Tact_Shutdown(0)`;
`bGeoscapeUi && bGameState` -> `GeoscapeUi_FreeAll(0)`; `bGeoscapeUi && !bGameState` -> **nothing**
-- the FreeAll call sits behind a second `bGameState` test that this combination fails (the two
redundant `BEQ loc_44D7C` after it are dead code). A geoscape save loaded over a live geoscape UI
frees no UI at all; that is the original's own path.

`Save_ReadStrateg`'s two pointer header fields (globals 0x1865A8 / 0x186594) are read
unconditionally in the original. The port models them as caller pointers, and a NULL must NOT skip
the 4 bytes -- skipping desynchronises the whole rest of the chunk (this exact bug corrupted every
item field in the first round-trip). The reader now sends the bytes to a scratch when the pointer
is NULL.

Hook seams (unported callees, cited without `@`): `Geoscape_InitUI (0x857f4, not ported)`,
`Base_CountOutboundTransferSpace (0x68044, not ported)`, `Base_CountItemQtyAcrossBases (0x67d68,
not ported)`, `Base_CountPsiSoldiers (0x65a08, not ported)`, plus the env-heavy `Render_Frame`,
`StartUi_Shutdown`, `Tact_Shutdown`, `GeoscapeUi_FreeAll`, `Tact_InitMission` calls. IDB: the
`LoadGame` function comment rewritten (the apObjects[4] claim removed), first arg retyped
`void *pSlotCtx`, comment at 0x44C94.

## The three budget leaves: `Base_CountOutboundTransferSpace @ 0x68044`, `Base_CountItemQtyAcrossBases @ 0x67d68`, `Base_CountPsiSoldiers @ 0x65a08`

All three are budget terms of the five `Base_Update*Needed` passes, and `LoadGame` now calls them
directly instead of through hooks. Ported in `src/pufo/game/pufo_geoscape.c`, unit-checked in
`test_geoscape` (9 checks).

- **`Base_CountOutboundTransferSpace`** sums a site's recovery load � `uRecoveredSoldiers +
  (i8)bRecoveredSoldierAssign + auItemQty[113] + auItemQty[112]` � of site `a2` (when not -1) plus
  every other site whose `bFlag1` equals `apBases[a3]->bUnk4`. With `a2 = -1` the `i != a2` test
  matches every index, so site 0 is scanned too � the needed-pass budget is the full matching-type
  sum. The original's first argument (`&0x159768`) is never read.
- **`Base_CountItemQtyAcrossBases`** is cargo volume, not item count: `ObjectRecFull.bCargoSize`
  (0xC6) times a site's `auItemQty` column, over the filtered object indices � the ARM's
  always-true `v6 > &Map` clause leaves the index set 1..69, 105..111, 122..124, 140 (object 0 is
  never counted). Block 1 is site `a2`'s own columns; block 2 scans the matching-type sites and
  adds `site[i].qty[v] * bCargoSize[v]` gated on `site[a2].qty[v] != 0` (0x67EE0: the gate reads
  `0x173*a2 + v`, the contribution reads the scanned site � two different sites, confirmed in ARM).
  The needed-pass call passes `a2 = -1`, and the original then gates on `aSmapSites[-1]` = 0x161D46
  � the buy/sell screen scratch below the site array, zero in the image and written only by
  screens � so at load time the term is 0. The port models a negative index as a zero gate.
- **`Base_CountPsiSoldiers`** counts soldiers with `nKind` (0xB6) == base and `bUnk149` (0x95) != 0
  � the psi marker. Budget of `Base_UpdatePsiLabNeeded` (divisor 10).

`LoadGame` also restores the strateg seed counter from the smap chunk (`nParam3` -> `pnSeed`, the
original's 0x18640C write at 0x40D94) and takes the site count (`nParam4`, 0x186410) for the budget
passes. IDB: comments rewritten at 0x68044 / 0x67d68 / 0x65a08 and at the three `Base_Update*Needed`
hosts (the "budget contributors unported" notes removed).

## The geoscape bring-up: Geoscape_InitUI @ 0x857f4

Ported in `src/pufo/ui/pufo_geoiinit.{h,c}`; tested by `tests/test_geoiinit.c` against the
shipped archives (the pads, sprites, research.dat and the 56 .cts files all come out of
main.dat / lang.dat for real).

The function is the builder half of the lifecycle whose teardown half is
`GeoscapeUi_FreeAll @ 0x86d20` (see the geouimanage section); the test runs InitUI(0),
FreeAll, InitUI(1) and FreeAll again, which is exactly the new-game and load paths.

### Facts the ARM corrected in the decompiler's rendering

| Site | Hex-Rays said | ARM says |
|---|---|---|
| 0x85A44 | `*0x186BC0 = 800; [1] = 400` (dwords) | `STRH 0x320,[R0]` / `STRH 0x190,[R0,#2]` - a u16 pair at +0/+2 inside a 0x4E204-byte block |
| 0x85DD8 | `unk_186F78 = &unk_13D620` (pointer) | literal pool value 0x13D620 = 1300000, read as a number by `BaseFacScreen_TryBuySelected @ 0x58910` |
| 0x86938 | `v75->wProgress = 0` | `STRH R4,[R1,#6]` - zeroes the u16 at topic+6 (in-memory `wUnk6`), not the save-order wProgress |
| 0x86534..0x865C4 | flat global writes | root click slots [8]=OpenScreen18, [9]=CloseScreen18, [10]=EnterBuildMode, [11]=OpenBaseInfo, [12]=OpenFunding (root dwords 0x186518/1C/20/24/28) |
| 0x86AA4 | `*(unk_113AE4+182) = 110` | `STRH [R0,#0xB6]` - ObjectRecFull.uFirePower of object 1 |
| 0x86A14 | `+188` write | u32 ObjectRecFull.uPrice, seeded from anObjectPriceInit (149 dwords at 0xAA2C0) for slots 1..nObjectCount-1 |
| 0x86A58 | name copy loop | object name at apObjects+4 (szName[32]), text records 1447..1594, a fixed 148 copies |
| 0x859E4 | `byte_160BBC[144] = -112` | `STRB 0x90` - the research entry count byte (144), the same byte every research/manuf loop gates on |

### The text-record tail is real zero data

`Assets_LoadStartup @ 0x384e0` reads TEXTS.DAT from the first archive that has it -
lang.dat, 1650 records - into aTextRecs @ 0xB38D0 at stride 205. The static span runs to
the next global at 0x113998 (1912 records) and is zero-filled .data: the binary has no
bytes there (verified: dump_pe reads zero and the IDB reads zero). InitUI's mission-site
names read records 1655..1710, which are past the loaded 1650, so with lang.dat present
every mission-site name is empty in the original too. The port now allocates the blob at
the full 1912-record capacity (`PUFO_TEXT_REC_CAPACITY`) so those reads land on zeros
instead of past the buffer, and the test asserts the names are empty.

### Tables read out of the binary

`tools/gen_geoiinit_tables.py` emits the five descriptor blocks (0xAB4B0 nav, 0xAB5F0
panel pair, 0xAB630 find pair, 0xABED8 root, 0xAC078 singletons) and the 149-entry price
table at 0xAA2C0. InitUI uses the root and singleton blocks plus the prices; the nav /
panel / find blocks belong to Geoscape_BuildNavControls (next unit). The singleton setup
order is the original's own: slot i gets descriptor {0,1,2,3,4,7,8,9,10,11,12,5,6}[i].

### Deviations (all deliberate, all in the header)

- The world block is allocated as the 4-byte pair, not 0x4E204 bytes; nothing reads the tail.
- Name copies are bounded to the destination field; the research.dat read is capped at the
  topic array; the price loop also stops at the 149-entry table; the 148 name copies also
  stop at nObjectCount. The original has no bounds at any of these.
- The three zoom levels are published as data (`SmapZoomSet aSets[3]`) instead of the
  level-0 globals 0x186E40 / 0x186B04 / 0x186BE0.
- The research entry count is written as the port family's u16; the original's store is a
  byte. Same value at startup.

### Still hooked out

`Game_StartNew @ 0x94554` (mode-0 branch) and `Geoscape_OnRadarListClick @ 0x854c4`
(status panel) are hooks. The 13 root click handlers and the craft-select handler are
env function pointers because `UiControl.pfnClick` takes no arguments.

## Geoscape_BuildNavControls and the new-base screen

Evidence: `Geoscape_BuildNavControls @ 0x67b1c`, `SoldierScreen_FindNextAtBase @ 0x6192c`,
`SoldierScreen_FindPrevAtBase @ 0x618c4`, `Ui_SetTransferFlag @ 0x619a8`,
`NewBaseScreen_Open @ 0x615d0`, `NewBaseScreen_TryConfirm @ 0x6152c`. Ported in
`src/pufo/game/pufo_baseui.{h,c}`, tested by `tests/test_baseui.c`.

### What the unit is

`Geoscape_BuildNavControls` (callers: `Game_StartNew @ 0x9542C`, `Base_EnterBuildMode @
0x84E6C`, `Ui_OpenResearch @ 0x84CBC`) builds three groups of heap `UiControl`s - ten nav
buttons (`apNavCtl` @ 0x159888), the soldier-list page pair (`apSoldierPanelCtl` @ 0x1598B0)
and the soldier-find trio (`apSoldierFindCtl` @ 0x1598B8). Each group: create all, wire all
`pfnClick`, then `UiControl_Setup(0, 0, desc)` for all. The descriptors are `aGeodescNav` @
0xAB4B0 (x = 1 for the first five and 120 for the second five, y = 229..301 step 18, pads
"emptyb"/"cemptyb"), `aGeodescPanel` @ 0xAB5F0 ((210,20) up/sup, (210,40) down/sdown) and
`aGeodescFind` @ 0xAB630 ((35,37) bnexts/bnext, (3,37) bprevs/bprev, (70,37)
emptyb1/cemptyb1); `tools/gen_geoiinit_tables.py --check` prints all three and the port's
tables match the image.

Five click handlers came with it:

- `SoldierScreen_FindNextAtBase` / `FindPrevAtBase`: move the soldier cursor
  (`nSoldierCursor` @ 0x159E08) to the next / previous soldier whose base byte
  `Soldier.nKind` (+0xB6, LDRB) equals `nSelectedBaseIdx` (0x159D81, LDRSB). On failure the
  cursor is untouched and the return is the u16 count (next) or -1 (prev); FindPrev never
  reads the count at all.
- `Ui_SetTransferFlag`: Sound_Play(17, 4), then `bTransferFlag` (0x159E10) = 1.
- `NewBaseScreen_Open`: sample 17; return if the signed base count (0x159D80) is 8; frame
  control (unk_186B00) to (224, 1) as two STRH stores; screen id 2; confirm control
  (unk_186BA4) gets `TryConfirm`; `Smap_ZoomOut` twice (2 -> 1 -> 0); `unk_187080` cleared;
  frame control gets the close thunk.
- `NewBaseScreen_TryConfirm`: sample 17; if the placement-pending byte (0x1870AC) is non-zero
  the funds test is skipped; otherwise `nFunds (0x1598D8) - aNewBaseCostByRace[kind-128] >= 0`
  also passes; on shortfall 0x1870C8 = 1, dword_1870B4 = 0, return 0. Passing runs
  `Geoscape_OnMapClick`.

### ARM facts the decompiler hides

- The cost read is `ADD R3, base, byte, LSL#2` + `LDR R3,[R3,#-0x200]` (0x61554..0x61568). The
  literal pool at 0x615A4 holds 0xAB488, so the table base is 0xAB488 and the index is
  `byte_159DF4 - 128` - the -128 is the compiler's fold of -0x200/4, not a second table.
  `byte_159DF4` (now `bPendingBaseKind`) is 128+kind; 0xFF marks "no pending placement" in the
  pointer handlers.
- 0x1870AC is read as a byte (LDRB at 0x6153C); the shortfall store into 0x1870C8 is a dword
  (STRMI at 0x61574) while every reader in the image loads a byte.
- The funds test is `SUBS R3, funds, cost` with MI = shortfall (0x61570): funds - cost == 0 passes.
- `NewBaseScreen_Open`'s gate is LDRSB == 8 (0x615F0); its x/y stores are STRH.
- FindNext/FindPrev compare LDRB (0..255) against a sign-extended LDRSB value: a negative
  selected base matches nothing.

### Deviations (all deliberate, all in the header)

- The original stores a NULL from a failed `operator new` and dereferences slot 0 anyway
  (0x67B74); the port guards every store and reports the failure through the return value.
- The original never frees the fifteen controls (no code outside this function touches the
  slots), so a second call leaks the first set; the port hands the pointers to the caller in
  `NavControls`.
- An out-of-range cost kind reads adjacent image bytes in the original; the port treats it as
  cost 0 (the 0xFF sentinel reads zero padding at 0xAB684).
- The 0x1870C8 shortfall store is a byte in the port (dword in the original, byte reads
  everywhere).

### Still hooked out

`SoldierScreen_Toggle @ 0x64af0` and `ResearchScreen_Open @ 0x616ac` (two of the fifteen click
handlers) arrive as env click thunks; `Geoscape_OnMapClick @ 0x8b4a8` (TryConfirm's tail call)
is a hook.

### `Game_StartNew` (done)

`Game_StartNew @ 0x94554` (3,976 B, `Geoscape_InitUI`'s mode-0 hook at 0x86974, R0 = the
0x1864F8 block) is recreated as `Game_StartNew(const StartNewEnv *)` in `ui/pufo_geoiinit.{h,c}`,
with `tests/test_startnew.c` (107 checks) running it over the real objects.dat and TEXTS records.
Every callee was already ported; the order and the state it leaves behind are pinned:

- 0x94584: every object record's `auPerBaseVal[0..7]` and `aItemQty[0..49]` cleared (of 52).
- 0x945D4: placement pending (0x1870AC) = 1, the three id counters (0x186544/48/4C) = 1, the five
  craft-type counters (0x18653C..0x186540) = 0.
- 0x94600: the per-base radar byte (unk_162008) from `Base_CountEmptyFacilityCells`, over the base
  count AS IT STANDS (the count is only zeroed later, at 0x94700).
- 0x94650: ten save-state bytes (0x18655D) = 1; strateg count byte (0x186530) = 0.
- 0x94680: the economy dwords 1625000 / 383000 / 1668000 / 968000 with the 0x14B4C8 zero buffer
  installed at 0x18659C; month counter, base count and selected base = 0; `GameTime_Reset`.
- 0x94730: the research mirror (byte_160BBC from the topics' bActive, unk_160B2C zeroed).
- 0x94798: eight bases' research (144) and product (200) columns cleared, including the +2176 /
  +1600 stores that reach into the NEXT base -- the same spill `Base_CreateNamedEntry` has.
- 0x947EC: the site count's LOW byte only (0x186410); all 200 site records cleared over
  `auItemQty[0..nObjectCount-1]`.
- 0x9485C: the facility grid reset, eight bases: buildDays and layers 0/1/2 zero, facType 0xFF.
- 0x94964: `Base_CreateNamedEntry(-6000, -6000, 0, "new")`.
- 0x94984: the 17 facility records (names from TEXTS 106..118, 463, 120..122; bFlag20 from the
  init table; bFlag21 = i; uCost from the 0x85B58..0x85F58 literal pool).
- 0x94DC0: ten `BaseFac_SetCellBuildState` calls -- hangar (2,0), access lift (2,2), living
  quarters (3,2), short-range radar (1,3), general stores (2,3), workshop (3,3), laboratory (4,3),
  general stores (5,3), hangar (0,4), hangar (4,4).
- 0x94E34: base stock +1 heavy cannon clip, +20 sectopod remains, +15/+10 operative corpses;
  funds 4147000; soldier and craft counts 0.
- 0x94E60: type-0 craft with 20 cargo adds (silacoid corpse 3, operative corpse 6, male/female
  corpse 2/2, hovertank wreckage 1, sectoid leader/commander 4/4, snakeman soldier/engineer 4/4,
  ethereal leader 4, chryssalid 10, riper/silacoid/celacid/sectopod/elerium/alloys/power source
  2 each, alien surgery + ufo navigation 1 each); then type-3 twice, each with fuel filled,
  floater navigator + floater leader, weapons 25/26 and ammo 10/3.
- 0x95310: eight soldiers via `Soldier_CreateAndClassify(0)`, each pinned to type 0 with a zero
  history (bUnk248 from the 0xABED0 table, {0,30,45,60} read at 2*type).
- 0x953A4: the five `Base_Update*Needed` passes with the budgets the LoadGame port lifts.
- 0x95400: `Base_GrantStarterItems(base 0, bGrant 0)` -- with the flag clear it adds nothing;
  alien pool addend 0; nullsub_7 carries no behaviour and is not ported.
- 0x95418: screen id 1, `Geoscape_BuildNavControls`, the research panel parked at (1000,1000),
  screen id 2, the confirm control gets `NewBaseScreen_TryConfirm`, `Smap_ZoomOut` twice,
  byte_187080 = 0; then `StrategList_MaybeSpawnCraft_Process(..., bSpawn 1)`.

**The starter facilities arrive already built.** All ten placement calls pass bClear 1, and
`BaseFac_SetCellBuildState` writes `nBuildDays = (bClear != 0) ? 0 : spec.nBuildDays`, so the
starter grid has placed facilities with zero build days (the hangar's 2x2 stamp 9/10/11/12 in
tileA is still written).

**`Ship_ctor` reads the live ship count for the cargo column.** 0x45C98 takes `nSlotIdx` from
`Map_nCraftCount`, and every object's `aItemQty[nSlotIdx]` is zeroed with it. The port's
`ShipCtorEnv` carries that count BY VALUE, so `Ship_SpawnAtSlot` now syncs
`pCtorEnv->nShipCount` from `pnCraftCount` before calling the ctor. Without the sync all three
starting craft share column 0 and each ctor wipes the previous craft's cargo. Found by
`tests/test_startnew.c`; the same by-value wart applies to `ShipCtorEnv.abTypeCounters`, which is
why the test asserts the ctor-side counters separately from the ones `Game_StartNew` zeroes.

**Craft types have no shipped names, and the port says so.** main.dat TEXTS ships five craft names
at 522..526 (Skyranger, Lightning, Avenger, Interceptor, Firestorm) and three auto-name prefixes at
137..139 (Skyranger-, Lightning-, Avenger-), but no code in the image indexes a text record by
`bCraftType`: `Game_StartNew` spawns types 0, 3 and 3 while the three starting prefixes differ, and
the cargo caps (140, 120, 260, 0, 0) give the "Firestorm" no hold. `CraftType` is mapped 1:1 onto
the shipped order as a recorded ASSUMPTION, not a verified mapping. The old port docs calling type
0 "the interceptor" had no evidence either.

**IDB enums.** `FacilityKind` (0..16, named from TEXTS 106..118/463/120..122), `ObjectSlot` (28
slots, named from TEXTS 1447+slot) and `CraftType` (redefined from an unevidenced lore mapping to
the shipped order) are now local types in the IDB, applied to `Ship_ctor`'s `nCraftType` and
`BaseFac_SetCellBuildState`'s `nFacType`, with comments at 0x45c20, 0x18653C, 0x186E48, 0x6aee0 and
0x94554. The same names are the port's enums, so a number in either place reads the same way.

Recorded deviations: the radar pass runs over the base count before it is zeroed (reproduced); the
site count store hits only the low byte (reproduced); `nullsub_7` dropped; the original's unused
first arguments (0x1864F8 for `Base_GrantStarterItems`, the MaybeSpawnCraft a1..a3) replaced by the
port's real state pointers; the soldier count held as i32 with u16 semantics, as LoadGameEnv does.

### `Geoscape_OnRadarListClick` (done)

`Geoscape_OnRadarListClick @ 0x854c4` — the status panel's `pfnClick`, installed by
`Geoscape_InitUI` — is ported as `Geoscape_OnRadarListClick(const RadarClickEnv *)` in
`game/pufo_baseui.{h,c}`, with 36 checks in `tests/test_baseui.c`.

The clicked row (`pStatusPanel->nUnk3032`) indexes `anLineAux`, and that value IS the strateg
item index: the panel lists strateg items and the aux column carries their list position. The
index is stored at 0x186BE4 (Hex-Rays renders it as the dword at `word_186BD4[8]`),
`byte_18709C[4]` is cleared, and the clock is unpaused (0x15961C = 0) before the dispatch. Six
branches then run, each pausing the clock (`GameTime_Pause @ 0x5d6ac` is a plain store of 1)
and editing the root message list at 0x158890 through the already-ported `UiLineList` leaves:

| item | action |
|---|---|
| type 1, active | drop line 0, insert at 0 with flag 10 and the item index in colour slot 0 |
| type 0, inactive | screen id 18, `OptionsScreen_OpenWithSound(item->nSiteId)` |
| type 1, inactive | 0x187054 and 0x159E74 = `Ship_FindIndexByStrategType`, append flag 11 with the ship index, radar pulse timer 250 |
| type 4, active | flag 0x18708C = 1, drop line 0, append flag 20 (row index, item index) — no timer |
| type 3, active | flag 0x187090 = 1, drop line 0, append flag 19, timer 250 |
| type 0, active | flag 0x187098 = 1, drop line 0, append flag 18, timer 250, return 250 |

**`GameTime_Pause` is not a toggle.** 0x5d6ac is `MEMORY[0x15961C] = 1` and a return of the
clock base — the port writes `GameTime.bPaused` directly, and the two tail branches do the same
store inline (0x856EC, 0x85764) rather than calling it.

**The original re-reads the item in every test** (six `PtrArray_Get` calls on the same index)
and bounds nothing: an out-of-range aux value dereferences whatever `PtrArray_Get` hands back.
The port reads the item once and returns 0 when the list has no such entry — the entry effects
(the index store, the flag clear, the unpause) still happen, which the test pins.

**The non-250 return value is the item pointer** (register residue at 0x85738). Nothing reads
it — the widget callback type is `void` — so the port returns 0 there.

The shell now wires `pfnRadarClick` to this handler (`app/pufo_world.c` owns the message list,
the selected-index/ship/strateg globals, the four 0x1870xx flags and the radar pulse timer);
the options branch runs through stub hooks until the options screen path is ported.


### `Base_CanPlaceHangarAt` (done)

`src/pufo/game/pufo_geoscape.c` behind `HangarPlaceEnv`, 27 new checks in
`tests/test_geoscape.c`. Its only code caller is `Geoscape_OnMapClick @ 0x8b688`.

**The IDA signature was wrong.** It read `(pGrid, nBase, nX, nY)`. The ARM sign-extends only R1
and R2 (0x629E0, 0x629E8), and the one it multiplies by 6 is R2 — so the arguments are
`(pGrid, nX, nY, unused)` and the base is not an argument at all: it is `nSelectedBaseIdx`, the
signed byte at `grid + 0x619` (0x629C8), 1561 bytes past a 1524-byte grid. The fourth argument is
never read. The IDB arguments are renamed and the function carries the corrected comment.

**An unfinished cell is accepted outright.** Both `buildDays > 0` branches (0x62A30 gated on
facType 11, 0x62A50 ungated) jump to the `MOV R0,#1` tail at **0x62D44**, which is a return of 1 —
not the layer2 tail Hex-Rays suggests. The facType gate on the first is redundant. A test pins
that the connectivity work is skipped entirely in that case.

**The rest is a connectivity test on the finished cells.** A 36-dword scratch map marks a cell 1
when `tileA != 0` **and** `buildDays == 0`; the tapped 2x2 footprint is cleared to 0; the first
remaining 1 seeds a flood that turns every 4-neighbour that is neither 0 nor 3 into 2 and marks
the source 3, sweeping until a sweep changes nothing. Any 1 left over means clearing the footprint
would cut that facility off, and the answer is 0. A cell that is still being built is scratch 0,
so it blocks the flood — a row of four with one under construction splits.

**The hangar's 2x2 footprint marking is dead code.** 0x62AC8..0x62B10 write 1 into the three
cells beyond the anchor, but the very same loop visits those cells in a later iteration and its
own `*v14 = v15` store (0x62AA8) replaces each mark. A test pins it: a hangar anchor with only
its own quadrant stamped links nothing, while the same layout with all four quadrants stamped
links through the tileA bytes themselves. Recorded in `bugs.tsv` at `0x62AD8`.

**The scratch array is overrun at row 5.** The frame is `SUB SP,SP,#0xA0` with the array at
sp+0x10, so it is 36 dwords; the footprint stores at row 5 reach scratch[36], scratch[37] and
scratch[42] — the function's own saved registers. Hex-Rays sizes the array as `var90[46]`
precisely because it sees those stores. The port clamps them (`bugs.tsv` `0x62B80`).

**The hangar tail is a capacity test.** `Base_CalcHangarCap - Base_CountShipsAtBase > 0`
(0x62D14..0x62D40), both callees passed `nSelectedBaseIdx`. The grid pointer the original hands
`Base_CountShipsAtBase` is dead: the callee forwards it to `BaseGrid_GetByte0x654`, whose first
argument is never read because it reaches the byte table through the global
`abFacilityGrid + 1620 = 0x159DBC` (0x6ACE4). That settles the arity question left open in the
IDB comment, which is now corrected; the port passes the 0x159DBC table explicitly.

**The non-hangar tail** (0x62D50..0x62D6C): `layer2[nY][nX] != 0` → 0, `facType == 0` → 0, else 1.
Empty ground cannot host a hangar, and neither can a cell the needed-flag layer has claimed. The
0xFF empty sentinel is not 0, so it passes.


### `BaseFac_FormatCostLabel` (done)

`src/pufo/game/pufo_geoscape.c`, 12 new checks in `tests/test_geoscape.c`. Callers:
`BaseFacScreen_DrawPlacementOverlay @ 0x59AE4` and `Geoscape_OnMapClick @ 0x8B550`, which copies
the result into the `aBaseFacilities` record's label field at 0x186C5A.

It writes `TEXTS[905 + nFacType] name` + `"-"` + `itoa(abBaseKindTable[nFacType])` into the shared
scratch string at 0x186B10 (`szRadarMsg`, the buffer the radar screen and
`Soldier_FormatDisplayName` also use) and then **increments** the table entry, so the number in
the label is the value before the call.

**`abBaseKindTable @ 0x1864DD` had no xref in the IDB at all**, which is why Hex-Rays renders the
access as an expression on the unrelated `pObj` global: every access is computed as
`0x1864F8 + nKind - 0x1B` for the load (0x6A46C) and `0x1864F8 + 0x65 + nKind - 0x80` for the
store (0x6A49C), both landing on `0x1864DD + nKind`. It is 15 bytes, all zero in the image, and it
is the same table `Base_CreateNamedEntry` reads for `BaseRec.bUnk16`. The IDB now names and
describes it.

**Kinds 15 and 16 spill.** Facility kinds run 0..16 while the table is 15 bytes, so those two read
and write into `pSmapPadZoom2 @ 0x1864EC` and the gap before `pSmapPadZoom1` (`bugs.tsv`
`0x6A49C`). The port sizes its copy for all 17 kinds.

The counter is loaded SIGNED and the `+1` is sign-extended before the byte store, so 127 wraps to
-128 and a negative entry prints a label with two dashes (`Fac908--3`). The first argument (the
grid pointer) is never read — R0 becomes the label buffer at 0x6A458 — so the port drops it.

### `BaseFacScreen_Init` (done)

`src/pufo/game/pufo_baseui.c`, 20 new checks in `tests/test_baseui.c`. Caller:
`Geoscape_OnMapClick @ 0x8B654`, the screen-2 branch taken without `bBaseFacConfirm`.

Five steps, in the original's order:

1. **The close handler first** (0x5A1C4): it is installed on the control slot at `unk_186B00`, which
   is then moved to x 224 / y 229. The SAME install runs again at 0x5A254, after the panel buttons
   exist, so the port installs it twice.
2. `0x15887C` and `0x158880` are cleared before anything is allocated.
3. Two controls are allocated (`operator new 0x20` + `UiControl_ctor`) into the pointer slots
   `0x158874` and `0x158878`. Their `+12` click handlers — `BaseFacScreen_PanelPrev` (0x5A214) and
   `BaseFacScreen_PanelNext` (0x5A220) — are stored BEFORE `UiControl_Setup`, and Setup does not
   overwrite them (the test pins this).
4. `UiControl_Setup` runs over the two 32-byte descriptors at `stru_AB220`: `{11,252,"up","sup"}` and
   `{11,277,"down","sdown"}`. `tools/gen_geoiinit_tables.py --check` prints them.
5. `0x158888` is cleared (0x5A274) and `BaseFacScreen_Toggle (0x589ec)` is tail-called; its register
   is the return value. The port returns 0.

`BaseFacScreen_Toggle` is not ported: the shell injects a no-op through `BaseFacInitEnv.pfnToggle`.

### `Geoscape_OnMapClick` (done)

`src/pufo/game/pufo_baseui.c`, 54 new checks in `tests/test_baseui.c`. Callers:
`NewBaseScreen_TryConfirm @ 0x6158C` (tail call), 0x15594, 0x857EC. The shell reaches it through the
confirm control (`app/pufo_world.c:World_TryConfirmClick`).

Branches on `nGeoscapeScreen @ 0x186BC4`; anything but 1 or 2 returns without touching anything.

**Screen 1** (0x8B660, gated on `bBasePlaceActive @ 0x1870BC`): `Base_CanPlaceHangarAt(grid,
nSmapTapX, nSmapTapY, 1)`. A refusal sets `bBasePlaceFail @ 0x1870B8` and writes nothing. An
acceptance clears the tapped cell:

- `tileA <= 8` or `>= 13` (0x8B6DC..0x8B878): the single cell — tileA/tileB/layer2 = 0,
  facType = 0xFF, buildDays = 0.
- `tileA` in 9..12 (0x8B6E4..0x8B7BC): the 2x2 footprint. The original's four stores look garbled;
  they decode to `144*nBase + 36*i + 6*nY + nX + {0,1,6,7}` with `i` walking tileA/tileB/layer2 over
  three iterations, plus facType 0xFF at the anchor only and buildDays cleared at +0, +6, +1, +7. The
  footprint starts at the tapped cell — the anchor quadrant is NOT cleared — and every store stays
  inside the 1524-byte grid.

**Screen 2** (0x8B4BC, gated on `bPendingBaseKind @ 0x159DF4 != 0xFF`):

- With `bBaseFacConfirm @ 0x1870AC` (0x8B4E4..0x8B614): commit. `apBases[0]` (renamed from
  `unk_114B2C`, Map + 0x1174) gets `nUnk8`/`nUnk12` = 0x159D94/0x159D98, `bUnk17` = the pending kind,
  `bUnk16` = 1; the `aBaseFacilities` record's `bType` (byte_186C24+0x1C) = kind + 0x80 and its
  `abName` at record+26 (0x186C5A) gets `BaseFac_FormatCostLabel`'s text; the strateg entry whose
  `nSiteId` equals the SIGNED `apBases[0]->bUnk4` gets nX/nY; screen -> 1; confirm and
  `bBaseFacDone @ 0x187078` cleared.
- Without confirm (0x8B634): `nSelectedBaseIdx @ 0x159D81` (LDRSB) takes the base COUNT at 0x159D80
  (signed), screen -> 7, tail-calls `BaseFacScreen_Init`.

Deviation: the strateg write is unbounded on a miss (0x8B5E4/0x8B5F8, bugs.tsv) — the port bounds it.
The port also returns an explicit status (0/1/2/3) where the original returns register residue.

### `BaseFacScreen_Toggle` (done)

`src/pufo/game/pufo_baseui.c`, 42 new checks in `tests/test_baseui.c`. Callers:
`BaseFacScreen_Init @ 0x5A27C` (tail call), `Geoscape_CloseUI` case 7 (0x8BF80/0x8BF84). The shell
installs it as `BaseFacInitEnv.pfnToggle`, so the build dialog is now built for real.

`bBaseFacDialogOpen @ 0x158884` decides the direction.

**Close** (0x58D98..0x58E04): clear the flag, release the two panel controls at 0x158874/0x158878
with `UiControl_FreePads`, raise the panel's `nLineCount` back by 1, then `UiPanel_FreeLines` and
delete the panel.

**Build** (0x58A10..0x58D74):

1. Count the `aBaseFacFlags` records 0..16 with `bFlag20` set (0x58A1C). With the shipped
   `g_abFacFlag20Init` table that is 10.
2. `operator new(0x1BB4)` + `UiPanel_ctor(count)` into `pBaseFacPanel @ 0x15886C`.
3. `nStyle` 5, `nFont` 16, the +3020 handler = `BaseFacScreen_TryBuySelected`, `nPosX` 120,
   `nPosY` 234, +7052 5, +7056 234, `nViewW` 230, `gap_0[0..2]` B4 B4 B4, `gap_0[3..5]` 46 96 AA,
   `gap_0[6..11]` 44 F4 FC 04 46 4B, +7088 0x96, +7072 0, +7073 1, +7076 1.
4. Rows (0x58BAC..0x58C0C) walk records 1..16; each flagged record gets its `szName` into the next
   `apLines` slot and `anLineAux` = the record number, which is the facility kind the buy handler
   looks up. A 17th row follows unconditionally, its name read from `aBaseFacFlags[17]` = 0x187024
   (a UiControl pointer slot -- bugs.tsv), aux 0.
5. `nLineCount -= 1` (0x58C68). The count loop covers record 0 while the row loop starts at 1, so
   with record 0 flagged the ctor allocated exactly the rows written and the close branch's `+1`
   restores the freed-buffer count.
6. With `byte_187074[4]` (the new-base mode) set: `nBaseFacPanelSel` = the row count,
   `bBaseFacPlaceArmed` = 1, and every cell of the selected base is wiped -- buildDays, tileA,
   tileB and layer2 to 0 and facType to 0xFF. The `6*(24*base + row + 18) + col` index Hex-Rays
   renders as `unk_1598DC[0].tileA.cell[0][...]` is the facType array at grid + 0x174 + 144*base + 108.

Deviations: the port owns only the panel -- the two controls are caller-owned, so their pads are
released but the objects are not freed; a failed allocation returns instead of storing through null
(0x58A7C, bugs.tsv).

### `BaseFacScreen_TryBuySelected` (done)

`src/pufo/game/pufo_baseui.c`, 13 new checks in `tests/test_baseui.c`. Installed by
`BaseFacScreen_Toggle` as the panel's +3020 click handler.

`sel = panel->nUnk3032` (0x58924), `kind = anLineAux[sel]` (0x5892C),
`cost = aBaseFacFlags[kind].uCost` (0x58944, record + 0x18). `nFunds - cost < 0` (0x5894C):
`byte_1870C8 = 1` as a **dword** (0x58960 -- every reader loads a byte) and `dword_1870B4[0] = 0`.
Otherwise `bBaseFacPlaceArmed = 1` (0x5897C) arms the placement, and with `byte_187074[4] == 0`
`nBaseFacPanelSel` takes `panel->nUnk3032` (0x58994).

Deviation: the port returns 1/0 where the original returns the panel pointer or `&dword_1870B4`, and
it refuses an out-of-range kind instead of reading the table out of bounds (0x58944, bugs.tsv).

### `SoldierScreen_Toggle` (done)

`src/pufo/game/pufo_baseui.c`, 49 new checks in `tests/test_baseui.c`. Nav control slot 2 installs
it (the `Geoscape_InitUI` write at 0x67B88) and the shell runs it through `World_SoldierToggleThunk`.

`Sound_Play(17)` (0x64B00) and `nGeoscapeScreen = 4` (0x64B14) happen on **either** direction; the
switch is the byte `bSoldierScreenOpen @ 0x159DE0` (LDRB 0x64B1C, STRB 0x64B24 / 0x651CC).

Open branch: `operator new(0x14)` + `PadImage_ctor` into `pSoldierPad @ 0x1598D4`
(0x64B2C..0x64B54), `PadImage_Load("soldier", 8)` (0x64B5C -- unconditional in the original),
origins 0 (0x64B6C, 0x64B74). Then four `operator new(0x1BB4)` + `UiPanel_ctor` panels:

| global | ctor lines | style | geometry |
|---|---|---|---|
| `pSolPanelEquip @ 0x159DF0` | 4 | nStyle 4, nFont 18, pfnClick = `SoldierScreen_EquipSelected`, bFlag7073 1, bUnk7088 -1 | x 55, y 102, nUnk7052 46, nUnk7056 102, nViewW 167, gap_0 44 F4 FC / 32 A0 BD / C8 F4 FC 04 46 4B |
| `pSolPanelStores @ 0x159758` | 4 | nStyle 4, nFont 18, bFlag7072 0, no pfnClick | x 190, y 102, nUnk7052 46, nUnk7056 102, nViewW 167, gap_0 44 F4 FC / 32 32 FA / 44 F4 FC 04 46 4B |
| `pSolPanelNames @ 0x15975C` | nAtBase | nStyle 13, nFont 18, pfnClick = `SoldierScreen_SelectListed`, bFlag7073 1, bUnk7088 -106 | x 10, y 70, nUnk7052 2, nUnk7056 70, nViewW 234, gap_0 B4 B4 B4 / 32 32 FA / F0 F0 F0 14 14 14 |
| `pSolPanelCraft @ 0x159DEC` | nAtBase | nStyle 13, nFont 18, bFlag7073 0, nUnk7076 2, bUnk7088 -106, no pfnClick | x 230, y 70, nUnk7052 5, nUnk7056 70, nViewW 40, gap_0 B4 B4 B4 / -- / F0 F0 F0 14 14 14 |

`nAtBase` (0x64D94..0x64DE0) counts `apSoldiers[i]` for `i < nSoldierCount` (`u16 @ 0x115350`,
Map + 0x1998) whose byte `Soldier+0xB6` equals `nSelectedBaseIdx @ 0x159D81` -- so that byte is the
assigned base, not a "kind".

Row pass (0x65018..0x6519C) walks the same roster and, for each soldier on the base, writes the name
at `Soldier+0xBA` into the names panel line (0x65058), `anLineAux[row] = ` the **roster** index
(0x65088, not the row number -- that is what the select handler reads back), the
`Soldier_FormatDisplayName` result into the craft panel line (0x65090), and the line colours:
`Soldier+0xB7 == 254` gives (250,150,150,20,20,20) on both panels (0x65148, 0x6516C), `255` gives
(150,150,250,20,20,20) (0x650F4, 0x65118), anything else sets nothing. Finally
`unk_186B00->pfnClick = SoldierScreen_CloseThunk` (0x651AC).

Close branch: `nGeoscapeScreen = 1` (0x651C4), clear the byte (0x651CC), `nLineCount = 4` on the
equip and stores panels (0x651E4, 0x651EC), then `UiPanel_FreeLines` + `operator delete` on equip,
stores, `pSoldierPad`, names and craft.

Deviations: the port sets `nLineCount` only when the panel pointer is non-null (0x651E4, bugs.tsv);
the original's `Soldier_FormatDisplayName` call passes a scratch buffer that Hex-Rays renders as
`&abFacilityGrid`, so the port takes the formatted name through `pfnFormatName` instead; the close
control keeps the shell's thunk because `SoldierScreen_CloseThunk` (0x64ae4) only tail-calls the
unported `Geoscape_CloseUI` (0x8b94c).

### `SoldierScreen_SelectListed` (done)

`src/pufo/game/pufo_baseui.c`, 4 new checks in `tests/test_baseui.c`. Installed by
`SoldierScreen_Toggle` as the names panel's +3020 click handler.

`nGeoscapeScreen = 5` (0x64AA8); `nSoldierCursor @ 0x159E08 = pSolPanelNames->anLineAux[
pSolPanelNames->nUnk3032]` (0x64AD0) -- the roster index the row pass stored -- and the original
returns `&nSoldierCursor` (0x64AD4).

Deviation: the port returns the cursor value instead of a pointer.

### `SoldierScreen_EquipSelected` (done)

`src/pufo/game/pufo_baseui.c`, 28 new checks in `tests/test_baseui.c`. Installed by
`SoldierScreen_Toggle` as the equip panel's +3020 click handler and wired in the shell through
`World_SoldierEquipThunk`.

`Sound_Play(17)` (0x619D8), then `pSol = apSoldiers[nSoldierCursor]` (0x619F4). The gate (0x61A1C)
is `Soldier.bUnk183 >= 0xFE || apCraft[bUnk183].bAtBase != 0`: `bUnk183` (+0xB7) is the assigned
craft index with 254/255 as the "no craft" sentinels and `bAtBase` is Ship +0x06, so a soldier on a
craft that is off-base cannot be re-equipped and the handler changes nothing.

The row is `pSolPanelEquip->anLineAux[pSolPanelEquip->nUnk3032]` (0x61A44) and the equipment is
`apObjects[121 + nUnitType]`:

| row | effect |
|---|---|
| 0 (0x61B30) | with `nUnitType` non-zero, `BaseRec_AddItemQty(base, apObjects[121 + nUnitType], 1)` returns the equipped item (0x61B60) and `nUnitType` goes to 0 (0x61B74) |
| 1..3 (0x61AF0 / 0x61AE0 / 0x61A6C) | `nUnitType` takes the row number; with an old type non-zero the new item leaves the base (`BaseRec_SubItemQty`, 0x61A80) and the old one comes back (`BaseRec_AddItemQty`, 0x61AC4); with zero only the new item leaves (0x61B04) |
| anything else | the tail only |

The tail (0x61B78) clears `bTransferFlag @ 0x159E10`. The base is `apBases[nSelectedBaseIdx]` in all
four calls (1117 dwords = Map + 0x1174), and the aux values the switch reads are written by
`SoldierScreen_DrawEquipPanels` (0x62d78, renamed this step from `Ui_DrawTextInputDialog`): it fills
the equip and stores panels with one row per item that has stock at the selected base and stores
0..3 in `anLineAux`.

Deviations: the port returns 1/0 where the original hands back `&bTransferFlag` or the soldier
pointer, and it bounds the cursor, the base index, the craft index and the old `nUnitType` to the
121..124 group (0x61A1C, bugs.tsv).

### `BaseFacScreen_DrawPlacementOverlay` (done)

`src/pufo/game/pufo_baseui.c`, 35 new checks in `tests/test_baseui.c`. Its only code caller is
`BaseFacScreen_DrawBuildDialog` (0x59EF8), so the shell wiring waits for that draw chain.

The selected facility is `pBaseFacPanel->anLineAux[nBaseFacPanelSel]` (0x58E20) and its grid type is
`aBaseFacFlags[kind].bFlag21` (record + 0x15, LDRSB). InitUI stores the record index into `bFlag21`,
so it is the kind; 11 is the hangar and it makes the footprint 2x2, carried everywhere as the byte
32 or 0.

`nSmapTapX` / `nSmapTapY` take the pending pair `nPendingPlaceX` / `nPendingPlaceY` (0x58E58,
0x58E60); with `bBaseFacPreviewActive` set they come from the cursor instead --
`tapX = (nSmapCursorX - nSmapOriginX) / 32`, `tapY = (nSmapCursorY - nSmapOriginY - 36) / 32`, the
ARM's `+31` before the `ASR#5` being C's truncate-toward-zero division. A tap outside 0..5 skips the
clamped 2x2 edge outline (the lines at `y = nOriginY + 196` and `x = nOriginX + 160`, always in the
invalid colour `10*(nMapPhase + 15)`).

The preview block (0x5917C) runs when `bBaseFacOverlayShown` or `bBaseFacPreviewActive` is set and
the cursor lies inside `0 < dx < 192 - span`, `36 < dy < 227 - span`; it raises
`bBaseFacOverlayShown` and scores the tap:

```
footprint = span ? tileA[r][c] + tileA[r+1][c] + tileA[r+1][c+1] + tileA[r][c+1]
                 : tileA[r][c]
```

With an empty footprint the four sides are walked and a neighbour counts only when its `buildDays`
byte is 0:

| side | guard | cells |
|---|---|---|
| up | `r > 0` | `tileA[r-1][c]`, and with span `tileA[r-1][c+1]` |
| down | `r < 5 - span/32` | `tileA[r+1][c]`, and with span `tileA[r+2][c]` (+384), `tileA[r+2][c+1]` (+385) |
| left | `c > 0` | `tileA[r][c-1]`, and with span `tileA[r+1][c-1]` |
| right | `c < 5 - span/32` | `tileA[r][c+1]`, and with span `tileA[r][c+2]`, `tileA[r+1][c+2]` |

The score is a byte (every accumulate is a STRB) and new-base mode forces it to 1. Two Hex-Rays
renderings are wrong against the ARM and the port follows the ARM: the "up" guard is the **row**
(0x592C8 `CMP R1,#0` -- R1 is added before the `MUL #6` that builds the cell index), not the column,
and the two span reads below are +384 = `tileA[r+2][c]` and +385 = `tileA[r+2][c+1]`, which Hex-Rays
collapses onto +385. The outline is four rects, `nIdx` 0 with `nColorLow` at the phase colour when
the score is non-zero and the other way round when it is zero.

The commit (0x599A0..0x59B38) needs a non-zero score (a zero one clears `bBaseFacPreviewOk`),
`bBaseFacPreviewOk` set, the tap equal to the pending pair and `bBaseFacPreviewActive`. The cost is
`aBaseFacFlags[kind].uCost`; `nFunds - cost < 0` writes the shortfall as a dword and clears
`dword_1870B4[0]`. Otherwise `BaseFac_SetCellBuildState(grid, nSelectedBaseIdx, bFlag21, tapX, tapY,
0)` writes the facility and `nFunds` drops. In new-base mode the label comes from
`BaseFac_FormatCostLabel(bPendingBaseKind)`, `Base_CreateNamedEntry` runs at the pending coordinates,
`aNewBaseCostByRace[kind - 0x80]` comes off `nFunds`, `BaseFacScreen_Toggle` closes the dialog, and
`bBaseFacPreviewActive` / `nGeoscapeScreen = 1` / new-base mode / `bBaseFacPreviewOk` are cleared.

Deviations: the port returns 1/0 where the original returns register residue; it bounds the panel
selection, the kind index and the pending kind (0x58E20, bugs.tsv); and it scores only a tap inside
0..5, which is every tap the in-bounds cursor test can produce.

### `Ui_AnimExpandBlit` + `Ui_AnimExpandBlit_IfEnabled` (done)

`src/pufo/ui/pufo_panel.{h,c}`, 19 new checks in `tests/test_panel.c`. Sixteen call sites across the
geoscape screens (`BaseFacScreen_DrawBuildDialog`, `RadarScreen_Update`, `Geoscape_Draw`,
`Geoscape_DrawScreen`, `Geoscape_DrawGlobeHover`, `PsiScreen_Draw`,
`ManufScreen_DrawFrame`, `BuySellScreen_Draw`, `BasePlaneScreen_DrawCraftDetail`,
`ResearchScreen_Draw`, `Ui_TextInputDialog`), and it is the helper the build dialog draws its panel
through.

Two globals hold the animation: `bUiAnimStarted @ 0x187080` says whether it has started (the first
call sets it and zeroes the progress), and `cUiAnimProgress @ 0x187058` is the SIGNED 0..100
progress. `nMode` picks how the rect grows: 0 the full rect; 1 a `50*progress` square anchored at
(x, y); 2 full width with height `50*progress` centred (`y` becomes `nH/2 - 25*progress`); 3 width
`50*progress` with full height; 4 a `50*progress` square centred (`x = nW/2 - 25*progress`,
`y = nH/2 - 25*progress`). Width and height are then clamped down to `nW`/`nH` and the origin back up
to `x`/`y` (0x95D28..0x95D4C), and only when all four equal the full rect does the progress byte go
to 100 (0x95D60..0x95D74) -- the animation is done exactly when the clamped rect is the requested
one. `SoftSurf_BlitSrcRect` draws the body and eight `SoftSurf_BlitRectProxy` calls the bevelled
frame: outer bars `(x, y, w+4, 6)`, `(x, y+h, w+4, 6)`, `(x, y+4, 6, h-2)`, `(x+w-2, y+4, 6, h-2)`
and inner bars `(x+2, y+2, w, 2)`, `(x+2, y+h+2, w, 2)`, `(x+2, y+2, 2, h)`, `(x+w, y+2, 2, h)`. The
colours come from `bUiFrameStyle @ 0x186566` (root + 0x6E): 1 gives outer 40,120,200 / inner
60,150,220, and 0 or 2 gives outer 10,80,90 / inner 20,90,100. Returns `progress == 100`.

`Ui_AnimExpandBlit_IfEnabled` (0x663c4) is the fixed-parameter wrapper -- panel index 4, source origin
0x46/0x4B, key 0xC8, mode 0 -- and it only draws when `bEnabled` is exactly 1; otherwise it returns
`bEnabled` unchanged. Its first argument (a grid pointer) is never read.

IDB note: Hex-Rays renders the progress byte as `MEMORY[0x187054][1]`, a dword-scaled index that
looks like 0x187055; the ARM is `LDRSB R0,[R6]` with `R6 = 0x187058`.

Deviations: a mode outside 0..4 or a style byte of 3 or more reads an uninitialised stack slot in the
original; the port falls back to the full rect and the ordinary frame.

### `BaseFacScreen_DrawBuildDialog` (done)

`src/pufo/game/pufo_baseui.{h,c}`, 23 new checks in `tests/test_baseui.c`. The whole build-dialog
frame, and the only caller of `BaseFacScreen_DrawPlacementOverlay` in the image.

Nothing draws unless `bBaseFacDialogOpen @ 0x158884` is non-zero (0x59B6C); the branches split on
`bBaseFacPlaceArmed @ 0x158888` (0x59B84), the byte `BaseFacScreen_TryBuySelected` sets when a
purchase is affordable.

Armed (0x59B84..0x59EF8): the funds line `aTextRec156 + nFunds + "$"` at (5, 20) in 130,130,250 /
20,20,90; `Ui_AnimExpandBlit(0, 228, 236, 85, idx 4, src 0x46/0x4B, key 0xC8, mode 0)`; then in
50,255,50 / 0,20,0 the selected record's name centred at y = 236 and the three row labels
`aTextRec83`, `aTextRec84` and `aTextRec85` at (6, 257), (6, 275) and (6, 293); then in 50,150,255 /
0,0,20 the record's `uCost` at (185, 257), the literal "50000" (`a50000 @ 0xAB260`) at (185, 293) and
`<days> <aFacDaysSuffix>` at (185, 275), days being `aFacBuildSpec[record + 0x15].nBuildDays`; then
the title `aTextRec86` at (6, 4) in 68,244,252 / 4,70,75, the `*unk_186B00` pad blit, the red pair
220,80,80 / 50,30,30 and `BaseFacScreen_DrawPlacementOverlay`.

Unarmed (0x59F98..0x5A118): `bBaseFacOverlayShown @ 0x15888C` is cleared (0x59FAC), the same panel
frame, the panel's own rows through `UiPanelMsg_Draw`, the title `aTextRec1148` at (6, 4), the funds
line again and the pad blit.

Shared tail (0x5A138..0x5A188): `byte_1870C8 == 1` and `dword_1870B4[0] == 0` -- the state an
unaffordable purchase leaves behind in `BaseFacScreen_TryBuySelected` and in the overlay's commit --
animates a 200x70 panel at (20, 130) through `Ui_AnimExpandBlit_IfEnabled` and centres `aTextRec406`
at y = 158.

The eight runtime labels are `aTextRecs @ 0xB38D0` records at stride 205, and the IDB now names them
`aTextRec83/84/85/86/156/406/1148`. `aFacDaysSuffix @ 0xBB422` is the one exception: record 154 + 4,
a slice rather than a record start.

Centring is `120 - w/2` with the ARM's negative fix-up (`MOVS`, `ADDMI #1`, `ASR#1` at 0x59CEC and
0x5A168): a negative width is nudged up by one before the halving.

Deviations: the original builds every string in the 56-byte stack buffer psz (sp+0x18) with unbounded
copy loops and three `strcat`s (bugs.tsv); it indexes `anLineAux[nBaseFacPanelSel]`,
`aBaseFacFlags[kind]` and `aFacBuildSpec[bFlag21]` unchecked, and the port skips the three record
rows instead. `UiControl_BlitActivePad` takes one argument in the original and calls `PadImage_Blit`
/ `PadImage_BlitEx(pPad, 0)` itself; the port keeps its existing injected-blit seam. The original
returns register residue; the port returns 1 when it drew and 0 when the dialog is closed. The shell
wiring waits for `Geoscape_DrawScreen @ 0x6c16c`, the draw that calls this one.

### `Soldier_FormatDisplayName` + `Ship_FormatDisplayName` (done)

`src/pufo/game/pufo_baseui.{h,c}` and `src/pufo/game/pufo_ship.{h,c}`, 18 new checks in
`tests/test_baseui.c` and `tests/test_ship.c`. The two craft-name formatters, and the point where it
became clear that the five craft labels are one per CraftType.

`Ship_FormatDisplayName` (0x666cc, 424 B, twelve call sites) picks the label from `Ship.bCraftType`
@ +0x02: `aTextRecs` records 137, 138 and 139 for types 0, 1 and 2 and 418 and 419 for types 3 and 4,
and the last two put a "-" (`asc_AB340 @ 0xAB340`) between the label and the number. The number is
`Ship.bType` @ +0x00 -- the per-craft-type ordinal `Ship_Ctor` took from 0x18653C..0x186540 -- not the
craft id.

`Soldier_FormatDisplayName` (0x6654c, 384 B) keys off `Soldier.bUnk183` @ +0xB7: 0xFF is record 93 and
returns early, 0xFE is record 140 (reached because `BCS` at 0x665AC skips the craft branch), and
anything below 0xFE looks the craft up and names it with records 137/138/139 plus `Ship.bType` -- but
only for craft types 0..2. Types 3 and 4 branch from 0x665CC past both the label and the number and
write nothing, so the original hands back the shared scratch string exactly as the previous call left
it.

Both write into `szRadarMsg @ 0x186B10`, the 108-byte scratch that ends at `nSmapTapX @ 0x186B7C`,
with unbounded copy loops and a strcat (bugs.tsv); the port writes into the caller's buffer with the
bound the caller supplies. A CraftType outside 0..4 makes `Ship_FormatDisplayName` fall out of its
switch and copy its own uninitialised 104-byte local into the scratch (0x66848..0x66864); the port
returns an empty string.

IDB: the seven labels are now `aTextRec93/137/138/139/140/418/419` -- every one an exact
`0xB38D0 + 205*n` record start -- and both functions carry a comment. On the shell side
`SoldierScreen_Toggle`'s craft column no longer goes through a stub: `World_SoldierName` calls the
ported formatter with the world's own copy of the scratch buffer.

### `RadarScreen_RollWeaponHit` + `RadarScreen_BlitOverlay` (done)

`src/pufo/game/pufo_geoscape.{h,c}`, 21 new checks in `tests/test_geoscape.c`. Two radar-screen
leaves, and the first look at the intercept-target state the radar screen keeps.

`RadarScreen_RollWeaponHit` (0x5db24) rolls `Game_RandU16(100)` once as the event gate and a second
time for the damage band: event 25 is a flat -50 under a 50 gate; 26 adds `RandU16(20) - 100` under
80; 27 adds `RandU16(20) - 120` under 90; 28 adds `RandU16(30) - 90` under 70; 29 adds
`RandU16(50) - 140` under 70; 30 -- the switch's default arm at 0x5DB70 -- adds `RandU16(30) - 230`
under 95. Anything else, or a gate that fails, changes nothing. The HP accumulator
`nInterceptTargetHp @ 0x159698` is written at 0x5DB90 and then mirrored into
`StrategItem.nHp @ +0x6C` of `strategList[nInterceptStrategIdx @ 0x159680]` at 0x5DBB0. Nothing
clamps the HP, so a big band drives it negative. The original dereferences that strateg lookup with
no bounds or null test; the port skips the mirror when the lookup fails.

`RadarScreen_BlitOverlay` (0x5d6d4) is the whole 16-byte body `PadImage_Blit(pRadarOverlayPad @
0x1596E4)`, the pad `RadarScreen_Init` (0x5F490) allocates.

IDB: `unk_159698` → `nInterceptTargetHp` and `unk_1596E4` → `pRadarOverlayPad`, with function
comments at 0x5d6d4 and 0x5db24.

Also fixed: the port cited `GameTime_Unpause @ 0x5d6c4`, which is four bytes inside the function --
the original starts at `0x5d6c0`. The citation in `game/pufo_clock.h` now names the real address, so
that function counts.

### The options screen's audio tab (done)

`src/pufo/game/pufo_baseui.{h,c}`, 48 new checks in `tests/test_baseui.c`. Seventeen functions that
bring the options screen up and wire its two volume radio groups.

`OptionsScreen_Init` (0x6be74) branches on a "the controls exist" byte at 0x159E8C. Set: clear it and
tear down nineteen controls. Clear: build seven list rows at `aOptionsListCtl @ 0x159E20` from the
seven identical records at 0xAB750 ("firebtn"/"cfirebtn"), build twelve radio buttons at
`aOptionsMusicBtn @ 0x159E44` / `aOptionsSfxBtn @ 0x159E5C` from the twelve identical records at
0xAB830 ("initg"/"cinitg"/"cinitg"), put the music row at y = 107 and the sfx row at y = 147 with
x = 46 + 25*i, install the twelve handlers, then run `OptionsScreen_RefreshVolumeHighlight` (0x6b8fc)
and `OptionsScreen_LayoutButtons` (0x6b6c4).

The twelve handlers (0x6ba14..0x6be10) are one shape: clear the group, light this button with
`nState = 2`, write the step, and mute only at step 0 — `Map_bMusicOff` / `Map_bSoundOff` become 1 for
step 0 and 0 for 1..5. Music ends with `Sound_ApplyMusicVolume`; sfx calls `Sound_ApplySfxVolume` and
then `Sound_Play(17)` so the player hears the new level. Four of the six music handlers store the step
before clearing the group and reach their button through the array; the other two clear first and name
the pointer. The order is not observable, so the port uses one body for all six. The level is the
original's own arithmetic — `4 * (muted ? 0 : 12 * step + 4) - 1`, muted giving -1 — observed in the
test through the backend seam (step 0 → -1, step 1 → 63, step 5 → 255).

`OptionsScreen_BindSaveSlots` (0x6b49c) and `OptionsScreen_BindLoadSlots` (0x6b588) are the save and
load tabs: scratch byte 1 or 2, seven slot handlers, rows centred at x = 120 - (uWidth >> 1) with
y = 40 + 22*i, the save hotspot dropped to y = 215, and `SaveSlot_FormatList` over the main menu's
slot context. The fourteen slot handlers themselves are not ported and enter as hooks.

IDB: the control block is now typed — `aOptionsListCtl[7]` at 0x159E20, `aOptionsMusicBtn[6]` at
0x159E44, `aOptionsSfxBtn[6]` at 0x159E5C, all `UiControl *` — which is what makes
`OptionsScreen_RefreshVolumeHighlight` read as `aOptionsSfxBtn[0]->nState = 2` instead of a byte
offset off `bTransferFlag`. The two descriptor tables at 0xAB750 and 0xAB830 are labelled and added
to `tools/gen_geoiinit_tables.py`. Comments on 0x6be74 (replacing a stale "NOT ported"), 0x6b6c4,
0x6b49c, 0x6b588, 0x6b8fc, 0x6ba14, 0x6bc1c and the flag byte at 0x159E8C.

Two ledger rows. `OptionsScreen_RefreshVolumeHighlight` indexes both button arrays with the stored
step and tests nothing (0x6B93C, 0x6B984) — a step outside 0..5 writes 2 into whatever pointer it
lands on; the port clamps. `OptionsScreen_LayoutButtons` centres the return hotspot on the SAVE
control's width: the uWidth read at 0x6B764 reloads R1 from the same R0 (unk_186BB4) while the store
at 0x6B778 lands on unk_186BB0. The port reproduces it.

### The options screen's save and load slots (done)

`src/pufo/game/pufo_baseui.{h,c}`, 15 new checks in `tests/test_baseui.c`. The fourteen handlers the
save and load tabs install: `SaveGame_Slot0..6_CloseUI` (0x6af80..0x6b148, 76 B each) and
`LoadGame_Slot0..6_InitOptions` (0x6b194..0x6b404, 104 B each). Fourteen copies of two bodies that
differ only in the slot number, so the port has one body each plus fourteen thin wrappers.

The save body: `MainMenu_nListMode = 4`, `SaveGame(&MainMenu_pSlotCtx, N)`, the tab byte back to 0,
`MainMenu_nListMode = 1`, then `Geoscape_CloseUI` over the 0x1864F8 control block as a tail call. Mode
4 is the value `MainMenu_Draw` (0x1dbb8) has no layout for, so the start screen drops its rows while
the slot file is being written; the port names it `PUFO_MAINMENU_MODE_SAVING`. The test sees the
transient mode through `SaveGame`'s own render hook, which runs before the file is opened.

The load body: tab byte 0, `nGeoscapeScreen = 0`, `MainMenu_nListMode = 3`, the "controls exist" flag
set, `OptionsScreen_Init` — which therefore takes its teardown branch and releases the options
controls before the load replaces the world — then `LoadGame(&MainMenu_pSlotCtx, N)`. A zero LOW BYTE
of the result restores list mode 1 and returns 1; any other value is returned unchanged. The test is
on the low byte, not the value, so a `LoadGame` that returned 256 would read as a failure; the port
reproduces the byte test.

Both drivers were already ported (`SaveGame @ 0x44988`, `LoadGame @ 0x44bbc`), so the handlers run for
real in the test: the slot path names a directory that does not exist, which is the one input that
makes each driver fail before it touches any save state. `pufo_save.h`'s header still claimed LoadGame
was "NOT ported"; that line is corrected.

IDB: comments on 0x6af80 and 0x6b194 describing both bodies and naming the six copies each, and the
two `Bind*Slots` comments updated now that the handlers they install exist.

### The loadout screen's stat panels, rearm and close (done)

`src/pufo/game/pufo_plane.{h,c}`, 44 new checks in `tests/test_plane.c`. Four functions, 2372 bytes:
`BasePlaneScreen_DrawStatLabelA @ 0x79e00`, `BasePlaneScreen_DrawStatLabelB @ 0x79fc4` (452 B each),
`BasePlane_RearmFromStores @ 0x78ed0` (536 B) and `BasePlaneScreen_Close @ 0x7c9e8` (480 B).

The two stat-label bodies are the same eleven operations and differ in exactly one store: A sets
`bStatPanelAOpen` (`0x161F18`) and B sets `bStatPanelBOpen` (`0x161F1C`). Those gates are what
`BasePlane_RearmFromStores` and `BasePlane_LoadWeaponStores` consult, so drawing a panel is what marks
it open. The frame is three nested `SoftSurf_BlitRectProxy` fills - 5,75 230 x 20*(n+1) at index 0x44
with 0xF4/0xFC, then 6,76 228 x 20*n+18 and 7,77 226 x 20*n+16, both inner ones index 0x14 with
0x50/0x63 - where `n` is the global at 0x161A38. Then the cyan pair (68,244,252)/(4,70,75) and three
column headers centred on 70, 164 and 210 at y 75.

The headers are text records 141, 142 and 143, and the original SHORTENS two of them by writing a NUL
straight into the loaded table: byte 7 of record 142 and byte 5 of record 143. In the shipped Russian
table those records are `Кол-во` (6), `Боеприпасы` (10, cut to `Боеприп`) and `Топливо ` (8, cut to
`Топли`) - the shortening IS the label, so the port writes through the caller's table exactly as the
original does, and the store lands before that header is measured. The truncation is idempotent.

`BasePlane_RearmFromStores` is two halves gated by those flags, each walking object index
0..Map_nObjectCount-1 and acting only on the record whose INDEX equals `Ship.bWeaponL` (left) or
`Ship.bWeaponR` (right) AND only when that record's per-craft column is non-zero: one back to the base,
one out of the craft, `BasePlane_LoadWeaponStores` for that weapon, then the weapon byte to 0. The walk
exists only to prove the craft carries the weapon, and that carry test is the function's whole gate.
Both gates are cleared at 0x790C4/0x790CC whether or not either half ran.

`BasePlaneScreen_Close` hands back every committed row when `bPlaneStoresCommitted` is set, over a
31..35 loop whose 33 arm is empty:

| slot | object | cargo column | base delta | cargo delta |
|---|---|---|---|---|
| 31 | 56 | +0x24 | 30 * n | 30 * n |
| 32 | 57 | +0x28 | 8 * n | 8 * n |
| 34 | 105 | +0x2C | 5 * n | 50 * n |
| 35 | 58 | +0x30 | 8 * n | 8 * n |

Slot 34 first reads as a mismatch and is not one: object 105 is weapon 29's ammo, which the game counts
in rounds of ten per base item, and `BasePlaneScreen_CommitStores @ 0x7ac38` moves the same 5-for-50 on
the way in. Then `nPlaneCrewQuota` (`0x161EE0`) is cleared and `Geoscape_CloseUI` runs over the 0x1864F8
control block.

IDB fixes found on the way: `byte_161AC4` was misnamed - the craft index is loaded with `LDR [R8]` at
0x7CA2C and 0x78E08, so it is a DWORD, now `nSelectedCraftIdx`. `0x161AD0` is one 106-entry dword table
(`anSlotCarryCount`), not four loose globals: `BasePlane_CalcWeaponStoresUsed` sums it over every object
index while `BasePlaneScreen_Close` reads 31/32/34/35. The three header records are now `aTextRec141`,
`aTextRec142`, `aTextRec143`, and the panel globals are `bStatPanelASel/BSel/AOpen/BOpen`. The global at
0x161A38 is READ here as the panel row count, which falsifies the "write-only residue" note the port had
been carrying for it: `pufo_app.c` and the flicker paragraph above are corrected.

### The hangar stores list: take a row, put one back (done)

`src/pufo/game/pufo_plane.{h,c}`, 66 new checks in `tests/test_plane.c`. `BasePlaneScreen_CommitStores
@ 0x7ac38` (948 B) and `BasePlaneScreen_AdjustSelectedQty @ 0x7a9b4` (644 B). The blocker the earlier
note named -- modelling the hangar screen's state block -- turned out to be five `UiPanel` pointers in
.bss, and the ARM says so literally: `LDR R0, =unk_161F00; LDR R0, [R0]; LDR R3, [R0,#0xBD8]`.

| slot | what it holds |
|---|---|
| `pPlaneSelRowPanel @ 0x161F00` | AdjustSelectedQty reads the selected ROW from `nUnk3032` |
| `pPlaneCommitRowPanel @ 0x161EEC` | CommitStores reads the selected ROW from `nUnk3032` |
| `pPlaneSlotPanel @ 0x161AC8` | `anLineAux[row]` is the row's SLOT id |
| `pPlaneAvailPanel @ 0x161A40` | `anLineAux[row]` is how many are still available |
| `pPlaneTakenPanel @ 0x161EF4` | `anLineAux[row]` is how many have been taken |

CommitStores bails when the available count is not positive, then splits on the slot. A slot OUTSIDE
31..35 is a weapon row and is gated on hold space: `BasePlane_CalcWeaponStoresUsed +
apObjects[slot].bCargoSize + Base_CalcWeaponStorageUsed` against `Craft_GetTypeCargoCap`, with 1 into
0x1870C8 (a dword store into a byte global) and 0 into `dword_1870B4` on refusal. A slot INSIDE 31..35
is a crew row, and here `Craft_GetTypeCargoCap` is used as a **craft class selector, not a capacity**:
cap 0 and cap 120 refuse outright, cap 140 needs the crew quota at most 3 with
`soldiersAboard + 4*quota <= 14`, cap 260 needs at most 4 and at most 26, and an unknown craft type
skips the crew gate entirely. The quota is pre-incremented BEFORE the gate and handed back on either
refusal -- which is why `BasePlaneScreen_Close` clearing it matters.

Then `Base_CraftWeaponItemId` must cover 30 / 8 / 5 / 8 for slots 31 / 32 / 34 / 35, else 1 goes to
0x1870C8 and **2** to `dword_1870B4`: the two refusal reasons are distinguishable through that residue
flag. Slot 33 has no object record, clears every gate, spends the quota and moves no stock. On
acceptance the base loses 30/8/5/8 items and the craft gains 30/8/**50**/8 cargo units -- the same
5-for-50 for object 105 that `BasePlaneScreen_Close` runs in reverse.

AdjustSelectedQty is the undo, and it carries one asymmetry the port must not smooth over: its cargo
debit is **clamped at zero** (`if (cargo < delta) cargo = 0` at 0x7AB10 and its three siblings), while
`BasePlaneScreen_Close` subtracts without a floor. Same rows, same deltas, different floor.

IDB: the five slots are now typed `UiPanel *` with those names, `unk_114A64` is `apCraft`, and both
functions carry the full gate description. `BaseRec_SubItemQty @ 0x5571c` was `static` in the port and
is now declared next to its mirror `BaseRec_AddItemQty`, because CommitStores needs it.

Covered by `TestCommitStores` and `TestAdjustSelectedQty`, 66 checks.

### The crew list's click: assign a soldier, or let them go (done)

`src/pufo/game/pufo_plane.{h,c}`, 24 new checks in `tests/test_plane.c`. `BasePlaneScreen_FillCrewPanel
@ 0x7a188` (620 B) and `Craft_CountWeaponSlotsCarried @ 0x664f0` (92 B).

The crew panel is `pPlaneCrewPanel @ 0x161EDC` and its right-hand column is `pPlaneCrewStatusPanel @
0x161A3C`, both built by `BasePlaneScreen_BuildListPanels @ 0x7a3f4` (not ported). The panel's aux
column carries a SOLDIER index, so a click means `apSoldiers[pCrewPanel->anLineAux[nUnk3032]]`, and the
function toggles that soldier between the selected craft and nothing:

- `Soldier.bUnk183 == 255` (not aboard anything) -> **try to assign**, gated by `Ship.bCraftType`:

  | craft type | rule |
  |---|---|
  | 0 | `soldiersAboard + 4 * weaponSlotsCarried + 1 <= 14` |
  | 1 | `soldiersAboard + 1 <= 12` -- weapon slots are not charged at all |
  | 2 | `soldiersAboard + 4 * weaponSlotsCarried + 1 <= 26` |
  | 3, 4, other | no gate |

  Refusing changes nothing whatsoever -- no colour, no name, no byte. Accepting paints both panels' row
  `(255,200,20)/(10,10,10)`, writes the craft index into `bUnk183`, and puts
  `Soldier_FormatDisplayName`'s string into the status column.
- `Soldier.bUnk183 != 255` -> **unassign**, with no gate on that half: paint `(240,240,240)/(20,20,20)`,
  put text record 93 (`Персон. броня` -- what an unassigned soldier becomes) into the status column, and
  store -1 into `bUnk183`.

`Craft_CountWeaponSlotsCarried` is the "4 crew per weapon" term, and its IDB name was wrong on both
halves. It was `Base_CountAmmoObjectsAtBase`, but it sums `apObjects[i]->aItemQty[a2]` over object index
31..35 only: the field is `aItemQty @ +0x3A` (`auPerBaseVal` is `+0x2A`), which is the **per-craft**
column, and objects 31..35 are the weapon slots, not the ammo records (those are 56/57/105/58). Both
callers pass a craft index -- this one passes `nSelectedCraftIdx`, and `OptionsScreen_Open` passes the
same register it hands `Craft_CountSoldiersAboard`.

Two housekeeping fixes found on the way: `docs/idb/functions.tsv` was stale against the IDB for
`0xa33c8` / `0xa3624` / `0xa3f5c` (it held pre-rename names), and regenerating the inventory from it
briefly flagged those three as unexplained. The dump is now synced and the inventory regenerated, so
`0x664f0` carries its new name through the coverage tooling. A citation of `BasePlaneScreen_BuildListPanels
@ 0x7a3f4` that this unit briefly introduced was removed -- that function is not ported, and the `@`
form credits it as one (1472 bytes of false coverage).

Covered by `TestCrewPanelClick` and `TestWeaponSlotsCarried`, 24 checks.

### Building the crew list's two panels (done)

`src/pufo/game/pufo_plane.{h,c}`, 43 new checks in `tests/test_plane.c`. `BasePlaneScreen_BuildListPanels
@ 0x7a3f4` (1472 B).

One byte decides the whole function: `bPlaneListPanelsBuilt @ 0x161F24`. Set, it is a teardown -- free the
two nav controls' pads and the controls (`pPlaneNavUpCtl @ 0x161AA4`, `pPlaneNavDownCtl @ 0x161AA8`), then
`UiPanel_FreeLines` plus delete on `pPlaneCrewPanel` and `pPlaneCrewStatusPanel`, and clear the flag.
Clear, it builds them, fills the rows, and sets the flag.

The build half is a fixed shape: two `UiControl`s from the descriptor pair at `0xABC48` (205,20 "up"/"sup"
and 205,40 "down"/"sdown", now `g_aPlaneListNav`, printed by `tools/gen_geoiinit_tables.py`) carrying
`BuySellScreen_ListPanelPrev` / `ListPanelNext` -- the same pair the buy/sell screen uses, and they page
**both** panels. Then two `UiPanel`s of 0x1BB4 bytes with `nLineCount = unk_161AB4 + unk_161AB8`: the crew
list at x 10 with a 230-wide view, the status column at x 190 with a 30-wide view, both y 70, `nStyle` 14,
`nFont` 16, colours B4/B4/B4 then F0,F0,F0 over 14,14,14 and 32,32,250, `bUnk7088` 0x96. The status panel
additionally clears `bFlag7073` (which `UiPanel_ctor` had set to 1) and sets `nUnk7076` to 1. The crew
panel gets `pfnClick = BasePlaneScreen_FillCrewPanel`.

The fill keeps a soldier when `Soldier.nKind` equals the selected base **and** the craft byte is 255 or the
selected craft index. Crew column = `Soldier.szName`; status column = `Soldier_FormatDisplayName`'s craft
label; and `anLineAux[row]` records the soldier index, which is exactly what `BasePlaneScreen_FillCrewPanel`
later clicks. A row already aboard the selected craft is painted (255,200,20)/(10,10,10); a row whose craft
byte is the 254 sentinel is painted (255,100,100)/(10,10,10) -- and that arm is reachable **only** when the
selected craft index is itself 254, because the row filter admits 255 or the selected index and nothing
else.

One thing that reads like a bug and is not: the status column formats `apSoldiers[nUnk3028 + i]` (the panel's
scroll cursor, added at 0x7A7B8) while the crew column shows soldier `i`'s own name. That would make the two
columns disagree on a scrolled list -- except the panel is allocated earlier in this same call, so its scroll
cursor is always zero when the fill runs. The addition is dead code as written; the port keeps it rather than
silently simplifying it away.

IDB: `0x161AA4` / `0x161AA8` typed `UiControl *`, `0x161F24` a byte, and the full build/teardown description
on 0x7a3f4.

Covered by `TestBuildListPanels`, 43 checks.

### Five false citations removed from the coverage metric (correction)

`src/pufo/game/pufo_baseui.h`, two comment lines. Five functions were being counted as recreated because
a comment named them in the citing form while saying in the same breath that they are not ported:
`Geoscape_OnPointer @ 0x872d0` (8576 B), `Geoscape_OnPointerUp @ 0x89450` (6992 B),
`Ui_TextInputDialog @ 0x632cc` (6096 B), `Geoscape_CloseUI @ 0x8b94c` (2100 B) and
`SoldierScreen_Draw @ 0x65290` (716 B). Both sites were prose lists of the form "the flag is read by
X @ addr, Y @ addr" -- a statement about the ORIGINAL's readers, not a claim about the port, but the
coverage tool cannot tell those apart because `@ 0xADDR` is the citation form.

They are now written in the paren form the codebase already uses for unported callees
(`Geoscape_CloseUI (0x8b94c)`), which names the function without crediting it.

Measured effect: credited functions 776 -> 771, bytes cited 339768 -> 315288 (56.7% -> 52.7%), and
`audit_names`' "credited addresses whose original name is not defined" goes from 5 to **0** -- every
address the port now credits is one the port actually defines. No behaviour changed; the number just
stopped lying.

### The soldier screen's frame (done)

`SoldierScreen_Draw @ 0x65290` (716 B) is the soldier screen's draw: three call sites inside
`Geoscape_Draw` (0x8c180) at 0x92DD0 / 0x92F38 / 0x93094, plus a data reference from the handler table
entry at 0x18A2B0. It is ported in `src/pufo/game/pufo_baseui.{h,c}` behind `SoldierDrawEnv`, with 45
checks in `tests/test_baseui.c`.

Three paths, separated by one screen id and one byte:

- `nGeoscapeScreen == 5` (0x652A4): not a draw at all -- it hands back `Ui_TextInputDialog(nSoldierCursor)`,
  the name-edit dialog `SoldierScreen_SelectListed` (0x64a9c) arms by putting 5 into the screen id.
  `Ui_TextInputDialog` (0x632cc, 6096 B) is still unported and enters as a hook.
- `bSoldierScreenOpen == 0` (0x652CC): nothing is drawn.
- open (0x652D4..0x65538): the "soldier" pad, the two list panels through `UiPanelMsg_Draw`, then the
  frame -- a 98x22 header bevel at 69,23 with light (0xB4) top and right edges and dark (0x64) bottom and
  left edges; two side rails at x 2 and x 235, each 24 climbing 1x8 blocks at y 5+7*k with index
  5*(k+20) (100..215) plus 24 falling 1x6 blocks at y 174+6*k with index 0xE6-5*k (230..115); and top and
  bottom rails on y 5 and y 315, each 24 5x1 blocks at x 1+5*k with 5*(k+20) and at x 119+5*k with
  0xFA-5*k (250..135).

Two geometry details the decompiler flattens, pinned down by pixel probes. The climbing side blocks are
8 tall on a 7 step, so each one repaints the previous block's last row: the 23rd block (y 159) still owns
165 and the 24th takes 166, the last climbing block reaches 173, and the falling half starts on 174. The
falling blocks are 6 tall on a 6 step and never overlap. The two halves of the top and bottom rails
overlap at x 119..120, where the last climbing block is drawn after the first falling one and wins.

Hex-Rays renders the two descending indices as `-26 - 5*k` and `-6 - 5*k`. They are not negative values:
the ARM computes `RSB R5, R3, #0xE6` (0x653E8) and `RSB R4, R3, #0xFA` (0x654E0) and stores all three
colour arguments with STRB into the `unsigned __int8` slots `SoftSurf_BlitRectProxy` reads -- the stack
frame really has `nIdx` at +4, `nColorMid` at +8 and `nColorLow` at +12, one byte each. The byte is what
matters, and the port writes the ARM's own expressions.

IDB fixes found on the way. `pSolPanelStores` (0x159758), `pSolPanelNames` (0x15975C), `pSolPanelCraft`
(0x159DEC) and `pSolPanelEquip` (0x159DF0) were 1-byte items although ARM loads them with `LDR` (0x64AB0)
and Hex-Rays dereferences them as panels; they are 4-byte `UiPanel *` now. The same shape was wrong for
the options screen's four panel slots `pOptPanelShips` (0x159E18), `pOptPanelRight` (0x159E1C),
`pOptPanelMidA` (0x159E78) and `pOptPanelMidB` (0x159E7C) -- `OptionsScreen_OnConfirm` (0x6cf18) reads
`MEMORY[0x159E18]->anLineAux[...]`, which only makes sense for a pointer -- and they are typed the same
way. `nSoldierCursor` (0x159E08) is left at 8 bytes with a comment instead: every direct access is a
single dword and 0x159E0C has no cross-reference of its own, so the extra width is unexplained rather
than provably wrong.

Still open in this family: `Ui_TextInputDialog` (0x632cc, 6096 B) itself, `SoldierScreen_CopyNameToEdit`
(0x6318c, 124 B), and `Geoscape_Draw` (0x8c180, 33744 B), the dispatcher that calls this draw.

### The hangar craft list, and the teardown that frees the whole hangar block (done)

`BasePlaneScreen_BuildPanels @ 0x7cc8c` (1768 B) with its two row clicks `BasePlaneScreen_SelectCraft @
0x7cbc8` (116 B) and `BasePlaneScreen_SelectListed @ 0x7cc3c` (80 B) land in
`src/pufo/game/pufo_plane.{h,c}` behind `PlaneBuildEnv` / `PlaneCraftSelectEnv`, with 58 checks in
`tests/test_plane.c`.

One byte decides the direction, and the teardown half turned out to be the more interesting one. With
`bPlanePanelsBuilt` (0x161F08) set, the function releases the two list panels **and the entire object
block `BasePlaneScreen_Init` (0x7d9e4) allocated at 0x161A48**: the backdrop pad at +0, five craft art
pads at +4..+0x14, six weapon icon pads at +0x18..+0x2C, four main controls at +0x30..+0x3C and seven
weapon controls at +0x40..+0x58. That is the hangar's whole allocation map, read off the four free loops
(0x7D2A0, 0x7D2D4, 0x7D304, 0x7D338), and it is what `BasePlaneScreen_Init` will need when it is ported.
The original frees through the slots without nulling them; the port nulls them.

The build half lists the base's craft in two panels -- names at x 10 view 230, status at x 230 view 30,
both eight rows of font 16 at y 110 -- and the row pass walks `aPlaneCraftTypeOrder` @ 0xABE28 (five
dwords: 0, 1, 2, 3, 4), so the list is grouped by craft type rather than roster order. Panel B's status
column and its row colours come from four `Ship` fields:

| condition | text | row colours |
|---|---|---|
| `bAtBase == 0` | 152 "������" | (150,150,250) / (25,10,10) |
| at base and `bRefueling` | 149 "��������" | (250,50,50) / (25,10,10) |
| at base, not refuelling, `uRangeRemain != uRangeMax` | 150 "�����" | (100,200,100) / (10,25,10) |
| at base, not refuelling, `uRangeRemain == uRangeMax` | 151 "�� ������" | (250,250,250) / (25,10,10) |

The last two read backwards for a craft sitting at base -- full range is labelled "on a mission" and
short range "ready" -- which is what the ARM does at 0x7D088..0x7D174 and what the port reproduces
rather than "corrects". Refuelling wins over both range arms because each range test re-checks
`bRefueling == 0`.

The two clicks differ in a way that looks like a bug and is not. Panel A's click reads panel A's own
cursor; panel B's click reads **panel B's** `nUnk3032` and then indexes **panel A's** `anLineAux` with it
(0x7CC54..0x7CC68, confirmed in ARM: `LDR R0,[=pPlaneCraftStatusPanel]`, `LDR R3,[R0,#0xBD8]`, then
`pPlaneCraftPanel`'s `anLineAux` at +0x13B4). It works because both panels are built with the same line
count and their cursors only ever move together through `UiPanel_ClickRow`. Panel A's click additionally
tests `apCraft[i]->bAtBase` -- `LDRB [R3,#Ship.bAtBase]` at 0x7CC14 -- and when the craft is at base sets
`bPlaneShowCraftDetail` (0x161F20) with a STRB; `BasePlaneScreen_Draw` (0x7d374) reads that byte first
and draws the detail view instead of the list. The flag is only ever set, never cleared.

IDB: `pPlaneCraftPanel` (0x161EFC) and `pPlaneCraftStatusPanel` (0x161ED0) typed `UiPanel *`,
`nPlaneSelCraftIdx` (0x161F04) an int, `bPlanePanelsBuilt` (0x161F08) and `bPlaneShowCraftDetail`
(0x161F20) bytes (both read with LDRB), `pPlaneBackdropPad` (0x161A48) a `PadImage *` as the head of the
object block, and `aPlaneCraftTypeOrder` (0xABE28) an `int[5]`. The comment on 0x7cbc8 said "if
Ship.bBusy != 0 sets bHangarCraftBusy" -- the field is `bAtBase` @ +0x06 and the flag is
`bPlaneShowCraftDetail`; the comment is corrected.

Still open in this family: `BasePlaneScreen_Init` (0x7d9e4, 1072 B) -- which the teardown map above now
fully describes -- `BasePlaneScreen_Draw` (0x7d374, 1648 B) and `BasePlaneScreen_DrawCraftDetail`
(0x7baf8, 3824 B).

### Building the whole hangar object block (done)

`BasePlaneScreen_Init @ 0x7d9e4` (1072 B) lands in `src/pufo/game/pufo_plane.{h,c}` behind
`BasePlaneScreenInitEnv`, with 36 checks in `tests/test_plane.c`. It is the allocation half of the pair
whose teardown half `BasePlaneScreen_BuildPanels` already carried, and the IDB already had the shape: its
local `BasePlaneScreen` type (size 92) is exactly the block at 0x161A48, so the port's struct takes that
name and layout -- backdrop pad at +0, five craft pads at +4, six weapon pads at +0x18, four main
controls at +0x30, seven weapon controls at +0x40.

What it wires:

- the backdrop pad, loaded with `"plane"` (`aPlane_0` @ 0xABEC4) at mode 0, origin 0,0;
- four main controls with the clicks `DrawStatLabelA`, `DrawStatLabelB`, `BuildListPanels` and
  `BuildControls`, positioned by the four `UiControlDesc` records at 0xABCC8 (now `aPlaneMainDesc`,
  ported as `g_aPlaneMainDesc`): 20,100 and 197,100 on the `initg`/`cinitg` pair, 20,200 and 20,240 on
  `emptyb1`/`cemptyb1`;
- seven weapon controls with `BasePlane_RearmFromStores` plus `SelectWeapon25..30`, positioned by the
  seven records at 0xABD48 (renamed `aPlaneWeaponDesc`, ported as `g_aPlaneWeaponDesc`): all `emptyb`/`cemptyb` at x 15, the
  first at y 92 and the rest at y 0;
- five craft art pads `baseb001..005` at mode 3, centred with `nOriginX = 120 - (uWidth >> 1)` at y 80;
- six weapon icon pads at mode 3, loaded `baseb049, 050, 051, 053, 054, 052` -- the last two swapped
  relative to their numbers, which is what the six `PadImage_Load` calls at 0x7DD30..0x7DD80 actually do
  -- and all six given `nOriginY` 95;
- `unk_186B00` moved to 224,1 with `BasePlaneScreen_Close` as its click, and `unk_186BA4` given
  `BuySellScreen_ApplyQtyChanges` (0x78d90, not ported) -- the same confirm slot the build dialog uses;
- a tail call into `BasePlaneScreen_BuildPanels`.

One original defect surfaced here and went on the ledger: `PadImage_ctor` (0x11000) stores only
`pData = 0`, so `operator new 0x14` leaves `uWidth` / `uHeight` / `nOriginX` / `nOriginY` uninitialised,
and the six `PadImage_Load` calls discard their result. A craft pad missing from the archives is
therefore centred on whatever the allocator handed back (0x7DC60). The port zero-fills the pad, so a
failed load centres at 120; the row is in `docs/parity/bugs.tsv` with `reproduced = no`.

IDB: `stru_ABCC8` and `stru_ABD48` renamed `aPlaneMainDesc` / `aPlaneWeaponDesc` (both are now in
`tools/gen_geoiinit_tables.py`, whose `--check` mode prints and verifies all eleven descriptor tables), and
the full wiring description is on 0x7d9e4.

Still open in this family: `BasePlaneScreen_BuildControls` (0x7b014, 2788 B), `BasePlaneScreen_Draw`
(0x7d374, 1648 B), `BasePlaneScreen_DrawCraftDetail` (0x7baf8, 3824 B) and `BuySellScreen_ApplyQtyChanges`
(0x78d90).


## The research screen's quantity buttons, and the two crew getters (done)

`ResearchScreen_IncSelectedQty @ 0x72ea0` (208 B) and `ResearchScreen_DecSelectedQty @ 0x72f70`
(140 B) are the research screen's "+1 / -1" allocation buttons, and they are where the port first
reaches the crew availability question: how many scientists a base has that are not already
committed to a project.

Both buttons address one entry of the research allocation column through the topic panel:

    LDR  R0, =pResearchTopicPanel ; LDR R0, [R0]      the panel pointer, 0x160C68
    LDR  R1, =nResearchTopicRowCache ; LDR R1, [R1]   the row, 0x160C74
    LDR  R3, [R0, R1,LSL#2 + 0x13B4]                 panel->anLineAux[row], the topic number
    MUL  R0, nSelectedBaseIdx, #0x90                  144 entries per base
    ADD  R1, R3, R0                                  topic + 144*base
    LDRH R3, [0x15EF08 + 2*R1 + 0x1324]              auResearchCol[topic + 144*base]

`nResearchTopicRowCache` is not a cursor these buttons move. `PlaySample7_SetFlags_Cache0xBD8`
@ 0x73328 writes it as `pResearchTopicPanel->nUnk3032`, the panel's own row cursor -- which is why
IDA's auto-name for that handler already said Cache0xBD8. The topic number in `anLineAux` is what
`ResearchScreen_RebuildTopicList` (0x74270, not ported) fills, the same trick the hangar craft list
uses to map a visible row back to a roster index.

Inc gates the store on two tests, in this order: `Base_GetScientistCount > 0` (0x72EB8), then
`Base_HasResearchSlotFree(Base_CountActiveResearch + 1)` (0x72ED4, 0x72EEC). Dec has no gate at all
beyond the entry being non-zero. Both truncate the result to 16 bits (LSL #16 / LSR #16 at 0x72F50,
and `ADDHI R3, R3, #0xFFFF` at 0x72FDC for the decrement), so 65535 + 1 wraps to 0 -- asserted in
the test.

Neither return value is a status code. On success both leave 4900 in R0, which is the research
column's byte offset from the 0x15EF08 anchor, register residue from `MOV R0,#0x1300` then
`ORR R0,R0,#0x24`. Inc's early exits return whatever the last call left there: the scientist count
when it is <= 0, so a negative availability is returned verbatim, or `Base_HasResearchSlotFree`'s 0
when the lab gate fails. Dec returns 4900 even when it stored nothing.

The two gates lean on `Base_GetScientistCount @ 0x6812c` (108 B) and `Base_GetEngineerCount @
0x681f4` (112 B), which are one expression over a different item and a different column:

    available = <crew item>->auPerBaseVal[base] - sum(<that base's quantity column>[0 .. nEntries))

The crew item is a slot of the map object pointer array, loaded out of the map root block:

    Base_GetScientistCount  LDR R0, =0x1139B8 / LDR R0, [R0,#0x2CC] -> 0x113C84, slot 113
    Base_GetEngineerCount   LDR R0, =0x1139B8 / LDR R0, [R0,#0x2C8] -> 0x113C80, slot 112

The slot numbering is pinned by the neighbours: `Map_apObjects105` at 0x113C64 puts the array base at
0x113AC0, so slot 112 lands on 0x113C80 and slot 113 on 0x113C84 exactly. What makes them engineers
and scientists rather than two anonymous items is `Base_SumCraftAtBase @ 0x6a230` (ported as
`Base_CalcMonthlyUpkeep`): it weights slot 112 with 20,000 right next to the soldier count and slot
113 with 30,000, and the geoscape site records use the same two offsets with the same weights
(`aItemQty[224]` and `aItemQty[226]`).

The subtraction has no floor: a base whose column already holds more than its total returns a
negative count, and every caller tests `> 0`, so a negative behaves exactly like zero. The port
keeps that instead of clamping.

IDB: `unk_113C80` / `unk_113C84` renamed `Map_apObjects112` / `Map_apObjects113` and typed
`ObjectRec *`; `Map_apObjects105` was carried as a 1-byte item and is now a 4-byte `ObjectRec *`
like its neighbours; `unk_160C68` renamed `pResearchTopicPanel` (`UiPanel *`); `unk_160C74` renamed
`nResearchTopicRowCache` (`int`); `unk_16022C` renamed `auResearchCol` and typed
`unsigned __int16[1152]`, which is 144 entries for each of the eight base slots and ends exactly at
0x160B2C where `abResearchZero` starts; and `abBaseCraftLoot` at 0x160C4C renamed
`nResearchEntryCount` (`unsigned __int8`) -- all 78 references to that address come from research or
manufacturing code that reads it as an entry count, nothing references 0x160C50..0x160C5F, and it
sits immediately after `abResearchActive` (byte_160BBC, 144 bytes), so the old name was wrong.

Deviation: the port takes the panel, the row cache, the count byte and the base index as arguments
and refuses, returning 0, when the panel, the column, the base index, the row or the topic number is
missing or out of range; the original derefs all of them blindly. `Base_GetItemQty` also refuses an
index past the eight per-base slots where the original indexes past the array, which is already on
record.

Still open in this family: `ResearchScreen_UpdateProgress` (0x72d14, 284 B), which inlines the same
gate in a loop, `ResearchScreen_StartSelected` (0x73240, 232 B), `ResearchScreen_CancelSelectedAndClose`
(0x73380, 116 B), `ResearchScreen_RebuildTopicList` (0x74270, 1700 B) and `ResearchScreen_Draw`
(0x73a34, 2108 B).


## The research screen's allocate, cancel and hourly accumulate (done)

`ResearchScreen_UpdateProgress @ 0x72d14` (284 B), `ResearchScreen_CancelSelectedAndClose @
0x73380` (116 B) and `ResearchScreen_AccumulateAllocated @ 0x74914` (148 B) finish the small
allocation family the two quantity buttons started.

`ResearchScreen_UpdateProgress` looks like a draw-time helper from its cross-references but is not
one: `ResearchScreen_Draw` installs it as the click handler of the control at 0x186B94 at 0x7417C
and positions that control at 100,164, so the two references to it are stores of a function
pointer. It is the allocate button.

Its shape is a fixed-count loop over a self-worsening gate:

    result = Base_GetItemQtyA(base)            0x72D30, the scientist TOTAL, not the spare
    if (result <= 0) return result             0x72D38
    do {
        if (Base_GetScientistCount > 0                          0x72D94
            && Base_HasResearchSlotFree(Base_CountActiveResearch + 1))  { 0x72DA8, 0x72DC4
            auResearchCol[topic + 144*base] += 1;               0x72DF4, truncated to 16 bits
        }
        ++iter; result = Base_GetItemQtyA(base);               0x72E1C, 0x72E20
    } while (iter < result);                                    0x72E28, CMP/BLT signed
    return result;

The total never changes inside the loop, so the iteration count is fixed, but both gates get worse
by one on every successful store -- the availability half because the getter subtracts the column
sum, the lab half because it asks for `active + 1` and `active` is that same sum. So the button
commits `min(spare, capacity - already active)` units in one press, then keeps looping without
storing until the counter reaches the total. With one free lab cell (capacity 50) and a total of
55 it lands exactly 50 and returns 55 -- asserted.

The quirk that took a test to see clearly: **both gates sum only the first `nResearchEntryCount`
entries**. If that count does not cover the topic being written, neither gate ever notices the
allocations this loop is making, and a total of 55 stores all 55 units over a capacity of 50. That
is the original's behaviour, not a port artefact; in a running game the count is the full 144, so
it does not bite.

`ResearchScreen_CancelSelectedAndClose` is the cancel button, installed twice by
`ResearchScreen_Init` (0x74AC0, 0x74BD0). It zeroes the same entry with no gate at all -- `MOV R2,
#0` / `STRH R2` at 0x733D4 -- and then tail-calls `Geoscape_CloseUI(&MEMORY[0x1864F8])`. The port
keeps the order even when its own guards skip the store: the close still runs.

`ResearchScreen_AccumulateAllocated` is the hourly tick, called from `Geoscape_TickHourly` at
0x5D30C. For each of the eight base slots -- a fixed `MOV R6,#8`, not the live base count, so empty
slots are walked -- it adds every non-zero `auResearchCol[base][i]` into
`aResearchTopics[i].wProgress`. The topic index is the column index, so all eight bases accumulate
into the same 144 records, and the `LDRH` / `ADD` / `STRH` triple at 0x74964 wraps at 16 bits. It
ends in a tail call to `ResearchScreen_RebuildTopicList` (0x74270, not ported), so the rebuild is a
hook and the function returns what the hook returns. Two ARM oddities are recorded rather than
smoothed over: the inner loop bound is re-read from `nResearchEntryCount` on every iteration that
saw a non-zero entry (`LDRBHI` at 0x7497C), which cannot change the outcome because nothing here
writes that byte; and the port clamps the walk to 144 entries where the original would run one
base's column straight into the next.

Deviation: the three functions take the panel, the row cache, the count byte, the base index and
the topic array as arguments and refuse or clamp where the original derefs blindly.

IDB: comments on 0x72d14, 0x73380 and 0x74914 record the handler-install sites, the fixed eight
base slots and the loop-bound reload. `unk_186B94` was left unnamed: `ManufScreen_DrawFrame` uses
the same control, so it is a shared dialog widget rather than a research one.

Still open in this family: `ResearchScreen_Init` (0x749a8, 592 B), `ResearchScreen_BuildPanels`
(0x733f4, 1600 B), `ResearchScreen_RebuildTopicList` (0x74270, 1700 B), `ResearchScreen_Draw`
(0x73a34, 2108 B), `ResearchScreen_Open` (0x616ac, 232 B) and `ResearchScreen_StartSelected`
(0x73240, 232 B).


## The manufacturing quantity buttons, the research pair's mirror (done)

`ManufScreen_IncSelectedQty @ 0x6dddc` (228 B), `ManufScreen_ReduceSelectedQtyToOne @ 0x6dec0`
(172 B), `ManufScreen_DecSelectedQty @ 0x6e09c` (160 B), `ManufScreen_DecEngineers @ 0x6e330`
(164 B) and `ManufScreen_UpdateProgress @ 0x6df6c` (304 B) are the manufacturing screen's quantity
controls, and they are the research pair with the crew and facility kinds swapped: engineers for
scientists, workshops for labs, `auManufCol` (0x15E19C, 200 u16 per base) for `auResearchCol`.

The same panel trick carries the selection. `pManufProductPanel` @ 0x15EE44 holds a product number
per row in `anLineAux`, and `nManufProductRowCache` @ 0x15EE3C caches that panel's `nUnk3032` row
cursor -- `ManufScreen_SelectListed` @ 0x6E944 writes it, exactly as
`PlaySample7_SetFlags_Cache0xBD8` does the research one. The entry index is
`auManufCol[product + 200*base]`, composed over the 0x159E98 product-block anchor plus 0x4304
(17156), and three of the buttons return that 17156 -- the same address residue the research
buttons return as 4900.

`ManufScreen_ReduceSelectedQtyToOne` is renamed in the IDB from `ManufScreen_GetSelectedProductId`.
It never returns a product id. It steps the selected product's quantity down one at a time while the
value is above 1 (0x6DF30..0x6DF54, re-reading `anLineAux` on every pass), which is a clamp to 1; a
zero entry is left at zero.

`ManufScreen_DecEngineers` is the one that is not a mirror. It works on the 32-bit half of the
product block, `aManufProductB` @ 0x15B79C (200 dwords per base -- the engineers assigned to each
product), and its rule is that the assignment may drop by one only while it is strictly greater
than `auManufCol[product + 200*base] + 1` (`LDR R4`, `LDRH R1`, `ADD R1,R1,#1`, `CMP R4,R1`,
`SUBGT` at 0x6E394..0x6E3B8). It returns the 0x159E98 anchor itself: `ADDGT R2, R0, R2` is the last
thing that touches R0, so the return is register residue, reproduced rather than interpreted.

`ManufScreen_UpdateProgress` is the manufacturing twin of `ResearchScreen_UpdateProgress`: the
engineer TOTAL fixes the iteration count, both gates get worse by one per store, and the loop
returns the total re-read at 0x6E084. One difference -- the gate byte is re-read on every iteration
(0x6DFA8), and when it is set the iteration skips the gates and the store but still counts.

All five open by reading `bManufQtyGate` @ 0x15EE50 and bail out returning that byte when it is
non-zero. **No code in the binary ever writes a non-zero value to that byte.** A search of the whole
listing for `15EE50` returns 17 hits and the only store among them is `ManufScreen_SelectListed`
writing 0 at 0x6E970, so in the shipped build the gate is dead and the buttons are never blocked.
Recorded in `docs/parity/bugs.tsv` as `dead_code`, reproduced by the port because the original has
it.

IDB: `unk_15EE44` renamed `pManufProductPanel` (`UiPanel *`), `unk_15EE3C` renamed
`nManufProductRowCache` (`int`), `unk_15EE34` renamed `bManufProductSelected` (the manufacturing
counterpart of the research screen's 0x160C80 detail flag), `byte_15EE50` renamed `bManufQtyGate`,
`unk_15E19C` renamed `auManufCol` (`unsigned __int16[1600]`), `unk_15B79C` renamed `aManufProductB`
(`int[1600]`), and `ManufScreen_GetSelectedProductId` renamed `ManufScreen_ReduceSelectedQtyToOne`
(`docs/idb/functions.tsv` and the regenerated `inventory-grouped.tsv` follow the rename). Comments
on 0x6dddc, 0x6dec0, 0x6e09c, 0x6e330 and 0x6df6c.

Deviation: the five take the panel, the row cache, the count byte, the base index, the gate byte and
both columns as arguments and refuse or clamp where the original derefs blindly.

Still open in this family: `ManufScreen_IncEngineers` (0x6e13c, 500 B), `ManufScreen_SelectListed`
(0x6e914, 120 B), `ManufScreen_ClearProductSlot` (0x6f8cc, 216 B), `ManufScreen_Draw` (0x6e3e0,
1332 B), `ManufScreen_DrawFrame` (0x6f9a4, 5400 B) and `ManufScreen_BuildPanels` (0x6e9e8, 2404 B).


## The two row clicks, and the research start button (done)

`ManufScreen_SelectListed @ 0x6e914` (120 B) and `ResearchScreen_StartSelected @ 0x73240`
(232 B) close the selection side of the two screens.

`ManufScreen_SelectListed` is the manufacturing product list's row click and the sibling of
`PlaySample7_SetFlags_Cache0xBD8` (0x73328): play sample 7, cache the panel's `nUnk3032` row cursor
into `nManufProductRowCache`, set `bManufProductSelected`, clear `bUiAnimStarted`, clear
`bManufQtyGate`. Two things the research sibling does not. It also clears the panel's **own**
`nUnk3032`, and it clears it with a byte store -- `MOV R2,#0` / `STRB R2,[R0,#0xBD8]` at 0x6E958 --
into a dword field, so only the low byte goes to zero and the upper 24 bits survive. A cursor of
0x123456 becomes 0x123400, which the test asserts. And it is the only place `bManufQtyGate` is ever
written.

`ResearchScreen_StartSelected` is where allocation becomes work. It clears the detail flag
(`unk_160C80`), plays sample 17, and then starts the selected topic only under two conditions
tested in sequence: the topic's `wProgress` is zero (`LDRH [topic*34 + 0x15EF08 + 0xA]` at 0x732C8)
and that topic's allocation for this base is non-zero (`auResearchCol[topic + 144*base]` at
0x732F8). The store at 0x732D8 reaches the same `wProgress` through the other anchor --
`ADDHI R3, R4, #4` then `STRHHI R2, [R3,#6]`, which is 0x15EF0C + 34*topic + 6, the same address.
That is the second confirmation that the live topic record starts at 0x15EF08 with `wProgress` at
+0xA, the first being `ResearchScreen_RefreshPanels` walking from 0x15EF2A = anchor + 34.

It then refreshes the three panels and re-runs both needed passes -- workshops and labs -- which is
why a research handler has to be handed the **manufacturing** column: `Base_UpdateWorkshopNeeded`
takes its budget from `Base_CountActiveManuf`. Then it tail-calls `ResearchScreen_RebuildTopicList`
(0x74270, not ported), so the rebuild is a hook.

Deviation: `ManufScreen_SelectListed` returns the cached row rather than the address of
`bManufQtyGate` the original leaves in R0, matching what the research sibling's port returns; and
both take their globals as arguments and refuse when one is missing.

IDB: comments on 0x6e914 and 0x73240 recording the byte-width cursor clear, the two-anchor
`wProgress` identity and the reason the manufacturing column appears in a research handler.

Still open in these two families: `ManufScreen_IncEngineers` (0x6e13c, 500 B), `ManufScreen_ClearProductSlot`
(0x6f8cc, 216 B), `ManufScreen_Draw` (0x6e3e0, 1332 B), `ManufScreen_DrawFrame` (0x6f9a4, 5400 B),
`ManufScreen_BuildPanels` (0x6e9e8, 2404 B), `ResearchScreen_Init` (0x749a8, 592 B),
`ResearchScreen_BuildPanels` (0x733f4, 1600 B), `ResearchScreen_RebuildTopicList` (0x74270, 1700 B),
`ResearchScreen_Draw` (0x73a34, 2108 B) and `ResearchScreen_Open` (0x616ac, 232 B).

## The site score, the build-mode handler, and the waypoint click (done)

Evidence: `Smap_SumSiteScores` @ 0x96894 (120 B), `Base_EnterBuildMode` @ 0x84e20 (124 B),
`Smap_SetWaypoint` @ 0x85440 (132 B). Ported as `Smap_SumSiteScores` in `pufo_geoscape.c` and
`Base_EnterBuildMode` / `Smap_SetWaypoint` in `pufo_baseui.c`. 61 checks in `test_geoscape.c`
(`TestSiteScore`) and `test_baseui.c` (`TestEnterBuildMode`, `TestSetWaypoint`).

**The site score reads the working slot, not the last used one.** Hex-Rays shows it plainly:
`v1 = &MEMORY[0x162030][MEMORY[0x186410][0]]` -- the site array indexed by the site *count*, not
count-1. That is not an off-by-one. `Transfer_ResetSite` @ 0x9690c clears exactly
`aSites[nSiteCount]`'s recovered-soldier fields and item quantities, so the record at the count is
the transfer flow's scratch slot and this function scores it. Three LDRH chains, and the ARM
constants pin the ranges: `ADD R2,R2,#0x9A` (154) then `LDRH [R3,#2]!` 28 times walks record
offsets 156..210, and `auItemQty` starts at offset 2, so that is `auItemQty[77..104]`;
`LDRH [R2,#0xE6]` is `auItemQty[114]`; `ADD R2,R2,#0x11A` (282) then `LDRH [R2,#2]!` 8 times is
`auItemQty[141..148]`. 37 halfwords, zero-extended into a dword accumulator, so the maximum score
37 * 0xFFFF = 2424795 never wraps. The port guards the array bound the original does not: at
`nSiteCount == 200` the original adds 742 to the array base and reads past `aSites[200]`.

**Colour slots are dwords in memory but bytes at every store.** This one is worth stating because
it is invisible until a value above 255 appears. `UiPanel_InsertLine` @ 0x5a294 masks its slot-0
argument with `AND R3, R3, #0xFF` at 0x5A2F4 and byte-loads slots 1..3 from the stack
(`LDRB R3, [SP,#arg_0/4/8]`); `UiPanel_SetLineColors` @ 0x5a450 does the same at 0x5A454. So no
store through either can put a value above 255 into a colour slot, while `UiPanel_GetLineColor`
reads them back as plain dwords. `Smap_SetWaypoint` shows the caller side of it: it stores the
full dword ship index to 0x159E74 and 0x187054 (0x85464, 0x85468), then *reloads it with `LDRB`
from 0x187054 at 0x85490* as InsertLine's slot-0 argument. The port casts to match. Asserted both
ways with a ship index of 300: the two globals keep 300, the re-inserted line's colour slot holds
44. The same byte load sits at 0x85560 in `Geoscape_OnRadarListClick`, and that call site now
carries the same cast.

**`Base_EnterBuildMode` is a widget handler, and its R0 is a red herring.** It calls
`Geoscape_BuildNavControls` with `MOV R0, R4` where R4 is `abFacilityGrid` -- but that function
takes no arguments at all (IDA types it `int Geoscape_BuildNavControls()`, and it reaches every
global it needs directly), so the grid pointer is dead. The order is: sample 17, the workshop and
lab needed passes for `nSelectedBaseIdx`, `nGeoscapeScreen = 1`, the nav rebuild, then park
`*unk_186BB4` at 1000,1000 with two `STRH` at +0 and +2. `unk_186BB4` is *not* one named control:
it is a scratch `UiControl *` slot with over a hundred cross-references spread across
`OptionsScreen_*`, `ManufScreen_*`, `ResearchScreen_*`, `Geoscape_InitUI`, `GeoscapeUi_FreeAll` and
`Geoscape_OnPointer`, which is why the port keeps it as an injected pointer rather than naming it.
Parking it at 1000,1000 -- off every 240x320 surface -- is how the original hides a widget it has
no slot for instead of freeing it.

Deviation, recorded: the original passes `nSelectedBaseIdx` through with no range check, so an
index outside 0..7 reads `auManufCol`/`auResearchCol` outside the eight-block array and, for base
8, writes the needed grid at 1632..1667 past its 1524 bytes. The port refuses the pair the way
`ResearchQtyIndex` already does; the sample, the screen id, the nav rebuild and the parked control
are unaffected either way.

Remaining in this cluster: `Smap_CancelTarget` (0x84254, 152 B) is still open because its callee
`Ship_AbortCourse` (0x5a64c) is not ported, and the two big geoscape pointer handlers
`Geoscape_OnPointer` (0x872d0, 8576 B) and `Geoscape_OnPointerUp` (0x89450, 6992 B) stay open.

## Clearing a product slot, and the engineer "+" button (done)

Evidence: `ManufScreen_ClearProductSlot` @ 0x6f8cc (216 B), `ManufScreen_ConfirmAndClose` @ 0x6e98c
(92 B), `ManufScreen_IncEngineers` @ 0x6e13c (500 B). Ported in `pufo_baseui.c` as
`ManufScreen_ClearProductSlot`, `ManufScreen_ConfirmAndClose`, `ManufScreen_IncEngineers`. 58 checks
in `test_baseui.c` (`TestClearProductSlot`, `TestIncEngineers`).

**The product table is four parallel 200-per-base columns, and clearing a slot zeroes one index of
each.** `idx = 200*base + product`, and the four stores land on 0x15B79C (u32), 0x15E19C (u16,
`auManufCol`), 0x159E9C (u32) and 0x15D09C (u16). The product argument is masked to a byte at
0x6F8D0 but the base is sign-extended from a signed byte at 0x6F8D4, and nothing bounds the sum --
`200*(-1) + 40` and `200*8 + 40` both go straight through the arrays. The port refuses the four
stores outside 0..1599.

**Products 119..121 clear a byte by reading it and subtracting it back.** After the four stores,
that range reads `apBases[nSelectedBaseIdx]->bUnk4` (0x6F968) and calls `BaseGrid_GetByte0x654`
followed by `BaseGrid_SubByte0x654` with *the amount the getter just returned*. So the net effect is
"zero the byte if it was non-zero" -- and it is not just a store, because `SubByte0x654` leaves a
zero alone. Two things this exposes. The byte is found by **type**, not by the selected base's
index: `Base_FindIndexByTypeByte` answers the first base that matches, so with two bases of the same
type the earlier base's byte is the one cleared (asserted). And the grid pointer these functions are
handed is dead -- `BaseGrid_GetByte0x654` reaches the table through the global at
`abFacilityGrid + 1620 = 0x159DBC`, which is why the port's `ManufClearEnv` carries that byte table
rather than the 1524-byte facility grid.

**`ManufScreen_IncEngineers` splits on the same 119..121 range, and the two halves differ in more
than their budget.** The hangar half's budget is `Base_CalcHangarCap - Base_CountShipsAtBase`, and
when that is zero it stores 1 into `byte_1870C8` (as a **dword**) and 1 into `dword_1870B4[0]`, then
re-reads `bManufQtyGate` *after* those two stores. The other half's budget is
`Base_CalcStoresCapacity` against `bCargoSize * aManufProductB[idx] + Base_CalcStorageUsed +
Base_CountItemQtyAcrossBases`, and at or above capacity it writes 1 into `byte_1870C8` and **0** into
`dword_1870B4[0]`. The residue differs between the two full paths, 1 versus 0 -- asserted both ways.

**The object whose cargo size gates the stores path is `apObjects[PRODUCT]`, not
`apObjects[base]`.** 0x6E214 is `ADD R2, R5, R2,LSL#2` where R2 still holds the product number read
from the panel's aux column, then `LDR R2, [R2,#0x108]` (the 0x1139B8 anchor plus 0x108 is
`apObjects` @ 0x113AC0) and `LDRB R2, [R2,#0xC6]` (`ObjectRecFull.bCargoSize`). Asserted with a
fixture where the base-indexed object would have blown the cap. Note also that this function uses the
aux cell's **raw dword** for both the column index and the object lookup, where
`ManufScreen_ClearProductSlot` masks it to a byte -- the two siblings are not identical there.

**`Base_CountShipsAtBase` is not just a craft count**, and `ManufScreen_IncEngineers` is where that
shows up in the UI: its free-slot test is `Base_CalcHangarCap - (craft homed at the base + the
0x159DBC byte for the base's type)`. Three hangar cells with `aGridBytes[0] = 3` is a full hangar
(asserted). A facility cell under construction is not capacity at all -- both capacity passes want
`buildDays == 0` -- which is what makes a half-built hangar or stores block the button.

IDB: comments on 0x6f8cc, 0x6e98c, 0x6e13c. `apBases` @ 0x114B2C was typed as a 1-byte global; it is
`BaseRec *[8]`, 32 bytes, ending exactly at `Map_nCraftCount` @ 0x114B4C. Retyped.

Open item found while doing this, not folded in: `apCraft` @ 0x114A64 is also typed as 1 byte, and
the gap to `apBases` is 200 bytes (50 pointers), but the port models it as 8. The real slot count is
not pinned by anything I have verified, so it is left alone rather than guessed.

Remaining in this cluster: `ManufScreen_CommitProduction` (0x6f34c, 1408 B), `ManufScreen_Draw`
(0x6e3e0, 1332 B), `ManufScreen_RefreshBaseTabs` (0x70ebc, 500 B), `ManufScreen_BuildPanels`
(0x6e9e8, 2404 B) and `ManufScreen_DrawFrame` (0x6f9a4, 5400 B).

## The base-info panel toggle, the assigned-ship sweep, and the four radar corners (done)

Evidence: `BaseInfoScreen_TogglePanelMode` @ 0x55754 (104 B), `Base_RemoveAllShips` @ 0x66874 (116 B),
`RadarScreen_SetCursorNW/NE/SW/SE` @ 0x5e768 / 0x5e808 / 0x5e93c / 0x5ea70 (160 + 308 + 308 + 160 B).
Ported as `BaseInfoScreen_TogglePanelMode` (pufo_baseui.c), `Base_RemoveAllShips` (pufo_ship.c) and the
four `RadarScreen_SetCursor*` (pufo_geoscape.c). 59 checks in `test_baseui.c`, `test_ship.c` and
`test_geoscape.c`.

**The toggle writes a number that is not the flag it just set.** `BaseInfoScreen_TogglePanelMode`
flips `byte_AB190` and writes a scroll cursor (`UiPanel.nUnk3028`, +0xBD4) into all three base-info
panels. When the flag was ZERO it becomes 1 and the cursor written is **0**; when it was non-zero it
becomes 0 and the cursor is **16**. The reason is the flag lifetime: `ANDS R3,R1,#0xFF` at 0x55764 sets
the flags, and everything between it and the `MOVEQ R1,#0` at 0x55784 is two MOVs and two conditional
stores -- none of them touches the flags -- so the MOVEQ answers the flag's ORIGINAL value. Hex-Rays
shows it as a second `if (v0) v1 = 0;` after the branch for the same reason. The test is non-zero, not
equal-to-one: 200 and 255 both take the 16 path.

**The assigned-ship list tracks the compaction it is being walked through.** `Base_RemoveAllShips`
walks `dword_161468[1..count]` and hands each entry's craft to `Ship_RemoveWithCargo` by its
`Ship.bId`, and it re-reads both the entry and the count on every iteration (0x6689C, 0x668BC). That
is not paranoia: `Ship_RemoveWithCargo` compacts `apCraft` and decrements every stored index above the
gap (0x36AEC..0x36B20), so a list of {0,1,2} becomes {0,0,1} then {0,0,0} and each position still
names the right craft. Asserted end to end: three listed craft, three removals, count zero. The tail
call is `Base_CountShipsAtBase`, so the answer is craft homed at the base plus that base's byte in the
0x159DBC table -- and that byte is found by TYPE, so two bases of one type share the earlier base's
byte (asserted: base 1 reads base 0's).

**The four radar corners are two pairs, and the pairs differ in three unrelated ways.** All four gate
on `byte_159714` (cleared by `RadarScreen_Init` at 0x5F4E0) and all four park the cursor at
NW 117,86 / NE 161,86 / SW 117,107 / SE 161,107 -- the values pair up as west/east and north/south,
and the x really is a pixel: `RadarScreen_Update` (0x60570, unported) loads it at 0x60F4C straight
into `SoftSurf_BlitRectProxy`'s x argument. The differences:

  - NW and SE write a fixed 560 into the range target. NE and SW compute
    `bound + 560 * (26 - level) / 114` with a signed divide, NE taking the **max** of the two bounds
    and SW the **min**.
  - NE and SW bail when both intercept slots hold -2. NW and SE have no such guard.
  - SE is the only one that sets the flip byte `unk_159718` to 1; the other three set it to 0.

The divide is `_rt_sdiv`, and this binary calls it with the dividend in R1 and the divisor in R0 --
the same convention `Base_UpdateWorkshopNeeded` uses for its `total / 50`, which is worth stating
because the register order is the opposite of what the names suggest.

**All four "copy" a string that does not exist.** Each one runs a strcpy-style loop from a source that
is all zero in the image (0x10360F, 0x1036DC, 0x1037A9, 0x103876) and each of those four addresses has
exactly four xrefs, all from its own setter. So nothing fills them, and what lands at `szRadarMsg`
(0x186B10) is a single NUL: the scratch string is cleared, not written.

IDB: comments on 0x55754, 0x66874 and the four setters. Two address corrections found while writing
the notes: `RadarScreen_Update` is 0x60570 (0x60F4C is an address inside it), and
`BaseInfoScreen_BuildPanels` is 0x557f4.

A port bug this unit caught and fixed: `RadarScaleRange` dereferenced its env before the null check
that `RadarSetCursor` performs, so `RadarScreen_SetCursorNE(NULL)` faulted. The test that found it is
in `TestRadarCursors`.

Remaining in this cluster: `RadarScreen_Init` (0x5f490, 3600 B), `RadarScreen_Update` (0x60570, 3804 B)
and `RadarScreen_RollDetection` (0x602b0, ~1100 B) -- the three that own the gate byte, the level and
the two bounds these setters poke. `BaseInfoScreen_BuildPanels` (0x557f4, 6084 B) and
`BaseInfoScreen_Draw` (0x5726c) own the three panels the toggle writes.

The geoscape base-tab strip, the inbound craft count, the buy/sell confirm and the race funding bonus (done)
---------------------------------------------------------------------------------------------------------------

Four handlers, none over 210 bytes: `Base_HitTestFacilityStrip` @ 0x671B0, `Base_CountInboundTransfers`
@ 0x67F78, `BuySellScreen_ConfirmSiteAndClose` @ 0x75090, `Funding_AddRaceBonus` @ 0x7debc.

**The base-tab strip is a column down the right edge, not a horizontal bar.** Hex-Rays renders the hit
test as `a2 < 211 || a2 > 237 || a3 < 3 || a3 > 28 * nBaseCount`, which reads like a y band and then an
x band, and the name says "facility strip". The ARM is the other way round and the drawer settles it:
`Base_DrawFacilityIconStrip` @ 0x6250C blits the selected cell's erase rect at x = 211, y = 28 * base + 3,
26x26 (0x62520) and walks the bases with `v5 += 28`. Both callers pass the cursor pair with x first --
Geoscape_OnPointer at 0x87F30 (R5 = nSmapCursorX, R8 = nSmapCursorY, set at 0x87E68 / 0x87E70) and
TransferScreen_Draw at 0x98980 -- so the box is x in [211, 237], y in [3, 28 * nBaseCount], and the hit
result `y / 30` is a BASE index. Geoscape_OnPointer stores it into nSelectedBaseIdx at 0x87F34;
TransferScreen_Draw stores it into unk_1870F4 at 0x98994 and indexes apBases with it at 0x989B0
(Map + 0x1174). A miss returns `(i8)(a1 + 0x619)`, the selected base read straight back out of the same
block, so the caller's unconditional store leaves the selection unchanged.

**Three gate bytes disable the strip by rewriting an argument.** When unk_1870EC (the transfer screen,
tested by Geoscape_CloseUI case 9 at 0x8BE30), unk_15883C (the base-info screen, case 2 at 0x8BDEC) or
byte_15EEF0 (the psi screen / base-stats panel, written at 0x716DC and 0x723AC) is non-zero, 0x6723C
loads 10 into the x register and branches back INTO the middle of the compare chain at 0x671E8. 10 < 211,
so the range test always misses. The port keeps the forced value rather than short-circuiting because
that is literally what the code does.

**A hit cancels a pending map tap.** 0x67250 clears the low byte of dword_1870B4[2] @ 0x1870BC. That
byte is the geoscape tap flag: Geoscape_OnPointer sets it to 1 at 0x87ED4 after computing the tap cell
coordinates, Geoscape_OnMapClick tests it at 0x8B660 before the hangar-placement branch, and clicking a
base tab drops the pending tap.

**The drawer's pitch and the hit test's divisor disagree -- a real bug.** Cells are 28 pixels apart but
0x67264 divides by 30. From the third cell down the top of a cell reports the PREVIOUS base: y = 59 is
the first row of the third drawn cell (28*2 + 3) and reports base 1, y = 199 is the first row of the
eighth (28*7 + 3) and reports 6. Only the last 3 to 11 rows of each cell reach their own base.
Reproduced, asserted, listed in bugs.tsv.

**The two base bytes are not part of the grid.** The hit test reaches the base count and the selected
base as abFacilityGrid + 0x618 and + 0x619, which is 1560 and 1561 -- 36 bytes PAST the 1524-byte
facility grid the symbol names. They are the base count and the selected base sitting in the same block.
The port takes them as their own pointers instead of indexing a 1524-byte array past its end.

**Base_CountInboundTransfers is the craft-side twin of Base_CountOutboundTransferSpace.** Same shape,
same type-match rule against apBases[nBaseIndex]->bUnk4, different per-site term: the signed pair
bRecoveredCraftCount (0x205) + bUnk518 (0x206), both read with LDRSB so 255 is minus one. Two call
shapes, both real: BaseInfoScreen_BuildPanels at 0x56634 passes nSiteIdx = -1 and then prints
`hangarCap - inbound - shipsAtBase` (0x5663C / 0x56644) as the base's free hangar slots, while
BuySellScreen_ValidateCapacity (0x75C58) and TransferScreen_ValidateCapacity (0x96548) pass the site
COUNT -- the transfer flow's working record -- so the pending site counts too.

**The buy/sell confirm promotes the working site into a real one.** Three tests in order: the cargo
volume of aSmapSites[nSiteCount] with the scan-sites flag OFF (0x750B0, a4 = 0), then
uRecoveredSoldiers at 0x202 (0x750DC), then auItemQty[112] + [113] + [117] + [118] (0x750E8..0x75104,
four LDRH loaded out of order). On any hit bFlag0 becomes 24 (0x75120) and the site count grows by one
(0x75124). Either way nFunds is copied into the buy/sell scratch dword at 0x161460 (0x75130) and
Geoscape_CloseUI runs as a tail call (0x7513C). 24 is the only value this byte is written with here; the
only other one seen anywhere in the binary is Base_DemolishFacility's 0xFF not-assigned marker (0x66D58),
and no reader of 24 was found -- recorded rather than guessed.

**Funding_AddRaceBonus maps race ids to funding slots non-monotonically.** One radar contact adds 2 to
one dword of the 0x186568..0x18657C block, and the mapping is a compare chain (0x7DEBC..0x7DF7C), not a
table: 0x80 and 0x81 and 0x88 feed 0x186568, 0x84 feeds 0x18656C, 0x82 feeds 0x186570, 0x83 feeds
0x186574, 0x85 feeds 0x186578, 0xFF feeds 0x18657C, and 0x86, 0x87 and 0x89 give nothing at all. Race
ids are 0x80..0x89 -- aNewBaseCostByRace @ 0xAB488 is ten dwords indexed by (race - 0x80) -- and 0xFF is
the no-race tile. FundingScreen_Init @ 0x5C0FC..0x5C144 is what gives the block its meaning: sum the
first five, ratio1 = 0x1865BC / (sum + 1), 0x1865BC += ratio1 * [5], ratio2 = 0x1865BC / (sum + 1), then
0x186580..0x186590 = [0..4] * ratio2. So [0..4] are five race scores and [5] is a multiplier on the
monthly total -- and [5] is the one dword of the block the save never writes. Both divides are
`_rt_sdiv` with the dividend in R1 and the divisor in R0, the same convention the radar corner buttons
use.

**Port bug found and fixed by this unit.** `Base_CountItemQtyAcrossBases` and
`Base_CountOutboundTransferSpace` bounded nSiteIdx against nSiteCount. They should not: five of the ten
cargo call sites (0x750B0, 0x75CC0, 0x9641C, 0x9692C, 0x97D30) and both extra outbound call sites
(0x75E34, 0x9666C) pass the site COUNT as the site index, because that is where the transfer flow keeps
the record it is working on. The bound silently dropped the whole term at the buy/sell and transfer
screens. Both now bound against PUFO_SMAP_SITE_COUNT, which is what the original leaves unbounded. The
buy/sell confirm test is what caught it: the cargo check failed with "got 0, want 24" before the fix.

**IDB updates.** Function comments on 0x671B0, 0x67F78, 0x75090 and 0x7DEBC; line comments on the three
strip gates unk_1870EC, unk_15883C and byte_15EEF0; `unk_161460` renamed `BuySell_nFundsSnapshot` (all
fifteen of its xrefs are inside BuySellScreen_*); `unk_186568`..`unk_18657C` renamed
`g_nFundScore0`..`g_nFundScore4` and `g_nFundMult`.

The three openers that also reset the clock (done)
--------------------------------------------------

**Evidence:** `ResearchScreen_Open` @ 0x616ac (232 B), `Ui_OpenBaseInfo` @ 0x84AF8 (256 B),
`Geoscape_PrepScreenOpen` @ 0x85194 (292 B). Ported in `src/pufo/game/pufo_baseui.c` behind one
`ScreenResetOpenEnv`; tests in `TestScreenResetOpen`.

**All three share a four-step prologue** no other opener runs: clear `szRadarMsg` (0x186B10), clear
`unk_186420`, walk the research "active" bytes, clear the shortfall dword at `byte_1870C8` with a
4-byte store, then `GameTime_Set5Sec`. The two clears are strcpy loops out of 0x10DA38, and 0x10DA38
is a block of zeros in the image with well over a hundred xrefs, every one of them the same shape --
so each copy writes a single NUL. It is the empty string, not a message. A line comment now says so
at 0x10DA38.

**The research walk has two details the ARM spells out and a source reading would miss.** The bound is
the byte at 0x160C4C, which is `byte_160BBC + 144`, and it is re-read only after a store happened --
0x6173C, 0x84B9C and 0x85234 are `MOVEQ` / `LDRBEQ` -- so a walk that changes nothing runs on the
count it loaded at the start. And the loop is a do/while: with a non-zero count entry 0 is always
examined, while a count of 0 skips the walk entirely even when `byte_160BBC[0]` is 1.
`ResearchScreen_Open`'s copy looks different because it uses `LDRB R3,[R0,R1]!` with writeback, which
reads like a running pointer; it is not, because the back edge at 0x61750 targets 0x6171C, which
recomputes R0 from the base every iteration. Deviation: the original bounds the walk only by a byte,
which could ask for 255 of the 144 entries; the port stops at `PUFO_RESEARCH_TOPIC_COUNT`.

**dword_186B08 is a spilled local, not a global with meaning.** `Ui_OpenBaseInfo` and
`Geoscape_PrepScreenOpen` store 0 into it before the walk and the index reached after it. It is the
dword immediately below `szRadarMsg` (0x186B10 = dword_186B08 + 8). The port keeps the counter in a
real local.

**The tails are where they differ.** `ResearchScreen_Open` plays sample 17 with the second argument 4
first, writes the screen id 8 BEFORE the Init, and tail-calls `ResearchScreen_Init` with the topic
array at 0x15EF08. `Ui_OpenBaseInfo` is the same shape with id 16 and `Ui_InitBaseInfo(unk_1872E0)`.
Both then get a second sample 17 from inside `GameTime_Set5Sec` -- the original really does play it
twice. `Geoscape_PrepScreenOpen` plays no sample of its own and writes no screen id: it clears the
clock pause byte at 0x15961C first (0x8519C), runs the prologue, removes line 0 of the root message
list (0x8525C), and re-pauses the clock when that line's flag is above 30 AND below 50 -- 0x85270 and
0x85284 are two separate `UiPanel_GetLineFlag` calls, not one value tested twice.

**Three more openers share the prologue and are the next unit.** The IDB already names them:
`Ui_OpenResearch` (0x84bf8, 288 B) and `Ui_OpenBaseInfoAlt` (0x84d18, 264 B) run the prologue and then
call `Geoscape_PrepScreenOpen` itself, so the clock dance and the 5-second tick happen twice in a row;
`Ui_OpenResearch` also calls `Geoscape_BuildNavControls(abFacilityGrid)`, writes 1000 into the two
halfwords at `*unk_186BB4` and `*unk_186BB4 + 2`, and sets id 8. `Geoscape_PrepScreenOpenSfx`
(0x852b8, 300 B) is `Geoscape_PrepScreenOpen` with a `Sound_Play(17,4)` inserted after the pause clear
and with `GameTime_Set5Sec` dropped entirely -- the only one of the six that leaves the tick interval
alone.

**IDB updates.** Function comments on 0x616ac, 0x84AF8 and 0x85194 recording the prologue, the two
thresholds and the tail-call targets; a line comment at 0x10DA38 saying what that zero block is.

The three siblings of the clock-resetting prologue (done)
---------------------------------------------------------

**Evidence:** `Geoscape_PrepScreenOpenSfx` @ 0x852b8 (300 B), `Ui_OpenBaseInfoAlt` @ 0x84d18 (264 B),
`Ui_OpenResearch` @ 0x84bf8 (288 B). Ported in `src/pufo/game/pufo_baseui.c` on top of the same
`ScreenResetOpenEnv` (the two that need more carry a `ScreenResetOpen2Env` wrapper); tests in
`TestOpenSfxAndAltVariants`.

**`Geoscape_PrepScreenOpenSfx` is the only one of the six that never touches the tick.** Its body is
`Geoscape_PrepScreenOpen` with `Sound_Play(17,4)` inserted right after the pause clear (0x852D0) and
with `GameTime_Set5Sec` removed outright: 0x8537C, the shortfall store, falls straight through to
`UiPanel_RemoveLine` at 0x85384. So opening a screen through this variant leaves the geoscape on
whatever tick it was already running.

**`Ui_OpenBaseInfoAlt` runs the prep and then the prologue again -- but not the whole tail twice.**
It sets `Ufopaedia_bReselectPending` (0x84D1C), calls `Geoscape_PrepScreenOpen` (0x84D24), and then
inlines the four prologue steps a second time. What repeats is the two string clears, the research
walk, the shortfall store and the 5-second tick. What does NOT repeat is the message-line removal and
the flag-in-(30,50) pause test -- those live only in `Geoscape_PrepScreenOpen`'s tail. That also means
the pause byte is cleared exactly once: if the prep pauses the clock, the second pass leaves it
paused. Screen id 16, tail `Ui_InitBaseInfo(unk_1872E0)`.

**`Ui_OpenResearch` runs the prologue without the tick, then the full prep, then the tick.** 0x84CA4
goes from the shortfall store straight to `BL Geoscape_PrepScreenOpen` at 0x84CBC, and only then
`GameTime_Set5Sec` at 0x84CC0. After that it rebuilds the fifteen nav controls
(`Geoscape_BuildNavControls(abFacilityGrid)`, 0x84CC8), parks the control at `unk_186BB4` at
(1000, 1000) with two `STRH` of 0x3E8 -- off every 240x320 surface, the same trick `Game_StartNew`
and `Base_EnterBuildMode` use -- writes screen id 8 and tail-calls `ResearchScreen_Init(0x15EF08)`.

**`byte_18706C` renamed `Ufopaedia_bReselectPending`.** Ten xrefs, four functions. `Ui_OpenBaseInfoAlt`
sets it on the way in; `Ufopaedia_RefreshEntry` reads it at 0x9A02C; `Ui_InitBaseInfo` consumes it at
0x9DEF4 -- when it is non-zero the ufopaedia category at `unk_160C60` is remapped through a 9x22
letter table onto a page and an index, `unk_1871F8` / `dword_1871D0[9]` / `unk_187300` / `dword_1872D8`
are filled from it, and the flag is cleared at 0x9E084. It is a "reselect the ufopaedia entry when the
base-info screen next opens" flag, and the port passes the byte through the env rather than owning it.

**IDB updates.** Function comments on 0x852b8, 0x84d18 and 0x84bf8; a line comment on 0x18706C.

The intercept launcher and the target dismiss (done)
----------------------------------------------------

**Evidence:** `Ship_LaunchIntercept` @ 0x5a64c (484 B) and `Smap_CancelTarget` @ 0x84254 (152 B).
Ported in `src/pufo/game/pufo_ship.c` and `src/pufo/game/pufo_baseui.c`; tests in
`TestLaunchIntercept` and `TestCancelTarget`.

**IDA had `0x5a64c` named `Ship_AbortCourse`, and the name is wrong.** It aborts nothing. Its own IDA
comment already said "Launch intercept", and all three of its callers are target-selection handlers
(`Smap_CancelTarget`, `Smap_ConfirmCraftTarget`, `Smap_OnPointerUp`). It is the mirror of
`Ship_RetargetTravel` @ 0x5a484, which sits 484 bytes earlier in the same block and sends a craft HOME
to its base; this one sends it OUT to a strateg item. Renamed to `Ship_LaunchIntercept` in the IDB and
recorded in `renames.tsv`.

**The one branch the "send home" version does not have.** The leg normally starts at the entry's own
position (`nStartX = nX`, `nStartY = nY`). But when `Ship.bAtBase` is non-zero the craft leaves from
its home base instead: `nStartX/nStartY` become `BaseRec.nUnk8/nUnk12` AND `nX/nY` are snapped to the
same pair, so a craft sitting in a hangar departs from the hangar rather than from wherever its entry
last drew. `StrategEntry_InitTravel` is then called with `bCopyStart = 1` -- the only caller in the
binary that ever sets it -- which writes the leg start back over `nX/nY` a second time.

**Four ship fields, in the originals' order:** `bWeaponsHot` (+0x34) = 1 at 0x5A790, `nStrategIdx`
(+0x38) = the target at 0x5A79C, `bAtBase` (+0x06) = 0 at 0x5A7A4, `bIntercepting` (+0x35) = 1 at
0x5A7AC. The proximity timer is restarted at 0 at 0x5A67C, the same store `Ship_RetargetTravel` makes;
`Ship_ClearWaypointIntercept` is the one that parks it at 0xFFFF instead.

**`Smap_CancelTarget` reads colour SLOT 1, not slot 0.** `Smap_SetWaypoint` puts the ship index in slot
0 of message line 0, and this handler takes the target strateg index from slot 1 of the same line
(0x84268) while the ship comes from the selected-ship dword at 0x187054. A test with a decoy 999 in
the other slot pins which one it is.

**`unk_1595D8` renamed `gUiRootPanel_nLineCount`.** The clock test at 0x842A4 is
`LDRB R0,[R4,#0xD48]` with R4 = 0x158890, and 0xD48 = 3400 = 200 flags + 200 * 16 record bytes: it is
the root panel's line-list count byte, read AFTER the removal. So the clock is un-paused only when no
message line is left standing. Ten xrefs, all the geoscape selection handlers.

**Two deviations, both about a bad index, recorded in bugs.tsv.** The original indexes `apBases` with
an unchecked byte, and it dereferences `list[nStrategIdx]` at 0x5A7B8 only after the timer, the two
start coordinates and all four ship fields are already written -- a stale target index faults halfway
through a launch. The port bounds `bHomeBase` and looks the target up before any store.

**IDB updates.** Function comments on 0x5a64c and 0x84254; `Ship_AbortCourse` renamed
`Ship_LaunchIntercept`; `unk_1595D8` renamed `gUiRootPanel_nLineCount` with a line comment.

The two "keep the course" selection answers (done)
--------------------------------------------------

**Evidence:** `Smap_ConfirmSelected` @ 0x845d8 (92 B) and `Smap_ConfirmSelectedAlt` @ 0x853e4 (92 B).
Ported in `src/pufo/game/pufo_baseui.c` on one `SmapSelectedEnv`; tests in `TestSelectedAnswers`.

**Both are eight instructions.** Read the ship index out of colour SLOT 0 of message line 0, answer it
with `Ship_ClearWaypointIntercept` -- the craft keeps the course it was already on and its proximity
timer is parked at 0xFFFF rather than restarted -- and then run the `Smap_ClearSelection` tail:
remove line 0, unpause only if the root line-list count byte is now zero, clear `bMapSelectionFlag`.
The dismiss half of the pair, `Smap_DismissSelected` @ 0x84428, was already ported in
`game/pufo_strateg.c` and is the same shape with `Ship_RetargetTravel` in place of the intercept
clear; it is not re-ported here.

**The Alt variant differs only in tail order.** It clears `bMapSelectionFlag` at 0x85420 and tests the
line count at 0x8542C; 0x845d8 has them the other way round. Nothing reads the flag in between, so the
two orders cannot be told apart from the outside. Recorded because the difference is real in the
binary and it is the only difference between them.

**IDB updates.** Function comments on 0x845d8 and 0x853e4.

The radar palette cycle and the range animation (done)
------------------------------------------------------

**Evidence:** `RadarScreen_CyclePalette` @ 0x5ed0c (232 B). Ported in `src/pufo/game/pufo_geoscape.c`;
tests in `TestRadarPaletteCycle`.

**The rotated block is a palette slot.** The ARM takes `aPalette @ 0x1472D0` as its base, walks a
pointer from base+0x60 down to base+0x4 and each step copies `[v+0x57FC]` over `[v+0x5800]`, then
stores the saved top entry into the bottom. base+0x5800 is `aPalette + 22 * 1024`, so the block is
slot 22 entries 0..24 -- 0x14CAD0..0x14CB30, 25 entries shifted up one place with entry 24 wrapping
into entry 0. A line comment now marks 0x14CAD0 as `aPalette` slot 22 entry 0.

**Who reads those 25 entries is not pinned down.** The IDB gives the block's two endpoints exactly
two xrefs each and all four are inside `RadarScreen_CyclePalette` itself; no other function names a
dword in 0x14CAD0..0x14CB30. The radar draw path does take `aPalette` as a base
(`RadarScreen_BlitMapBackdrop` @ 0x5eb10 at 0x5EBFC, `RadarScreen_Update` @ 0x60570 at 0x607E8,
`RadarScreen_Init` @ 0x5f490 at 0x5FC30), and a rotating palette is what an animated map backdrop
needs, but none of those three has a resolved reference to slot 22 specifically. Recorded as an open
question rather than asserted.

**The range animation is four conditional steps, not one.** Up, down, up, down, each storing:
`if (cur < tgt) cur++; if (cur > tgt) cur--; if (cur < tgt) cur++; if (cur > tgt) cur--;`. The pairs
are an overshoot-and-correct -- a value two or more away moves by two, a value one away moves by one,
and a value already at the target does not move at all. Then, only when the flip byte `unk_159718` is
set AND the value is still below the target, up to four more single steps. The flip branch is upward
only: there is no matching branch for a value still above the target, which is why the SE corner --
the one setter that sets the flip byte -- animates differently from the other three.

**`unk_15965C` renamed `nRadarRangeTarget`, `unk_15966C` renamed `nRadarRangeCur`.** The four corner
setters write the target and this function is the only thing that moves the current value, which is
what the earlier note in this file already said in prose: setting the target is not the same as
setting the range.

**IDB updates.** Function comment on 0x5ed0c, line comments on 0x14CAD0, 0x15965C and 0x15966C, and
the two renames.

Confirming a map target for a craft (done)
------------------------------------------

**Evidence:** `Smap_ConfirmCraftTarget` @ 0x842ec (316 B). Ported in `src/pufo/game/pufo_baseui.c` on a
`SmapConfirmTargetEnv`; tests in `TestConfirmCraftTarget`.

**The waypoint clear sits inside the type-5 branch, not after it.** If the craft is intercepting and
the item it chases is `nType 5`, that waypoint is removed and `bIntercepting` is cleared (0x84374). If
the old target was a real contact, or the craft was not intercepting at all, the flag is left alone --
the ARM's two early exits (`BEQ loc_8437C` on the flag, `BNE loc_8437C` on the type test) both land
past the store.

**0x186414 / 0x186418 are not viewport fields.** They are 0x2C and 0x30 past the 36-byte viewport block
at 0x1863E8, so they fall outside it, and each has exactly one xref in the IDB: this function. Their
writer is in the unported geoscape pointer path, so the port takes them as inputs and a line comment
at 0x186414 says so rather than inventing a `SmapView` field.

**The ship index is read out of the message line twice** (0x842FC and 0x843E0), with the strateg list
add and `GetCount() - 1` in between. Nothing in between touches the message line, so the two reads
agree -- but the second one is what the launch actually uses.

**`StrategList_RemoveAt` ignores its first register argument.** 0x82AB0 loads the Map context
`0x1139B8` itself and reads only R1, which is why the `MOV R0, R6` before the call is dead code.

**Zoom scaling is visible in the added item.** `StrategList_AddItem`'s type-5 branch scales the click
into level-0 world units by the current zoom mode: 300 at zoom 1 becomes 399, because the scale is the
double 1.3333 and `dtoi` truncates toward zero.

**IDB updates.** Function comment on 0x842ec, line comment on 0x186414.
The wrapped map distance and the craft range gate (done)
--------------------------------------------------------

Four functions, all of them reached from the geoscape minute tick, and one of them renamed.

`Smap_DistWrapped @ 0x7df80` is the distance the range gates use. The x leg is the map cylinder
-- `min(|x2-x1+800|, |x1-x2+800|, |x1-x2|)`, the same three the detect pass builds at 0x813D0 and
the travel setup builds again, so the port reuses its own `WrappedDx` -- and the y leg is the
plain `|y1-y2|`: the map wraps sideways only. A delta of exactly 800 wraps to zero, which is what
pins the wrap width at 800 rather than the 1600 an older comment claimed. The result is
`(int)(sqrt((double)(dx*dx) + (double)(dy*dy)) * 50.0)`: each square is an integer product
converted on its own through `_itod` and added through `_addd`, the CRT sqrt scales nothing, the
50.0 is the `_muld` second operand (its high word 0x40490000 sits at 0x7E018) and `_dtoi`
truncates. The scale is applied before the truncation, not after.

`Smap_CanInterceptReach @ 0x7e074` is renamed **Smap_CraftRangeExhausted**. Nothing in it tests an
intercept. It finds the strateg item that carries the craft (`StrategList_FindByShipId` at
0x7E084, `PtrArray_Get` at 0x7E094), reads the craft's home base out of `apBases`
(0x7E0AC..0x7E0C0) and measures `Smap_DistWrapped(home base, that item)` -- the item found is the
craft's own position entry, so this is the distance from home to where the craft now is, not to
any target. The per-minute burn is `(int)(uFuelCap / 60)` (`_rt_sdiv` at 0x7E0EC, 60 in the
divisor register) scaled by `uRangeMax / N`, and the five `_muld` operands in the class dispatch
turn out to be exact reciprocals: 1/30000, 1/40000, 1/64000, 1/12000 and 1/32000 for craft class
0..4. When `uRangeRemain` is above the burn it is decremented by it, so a craft parked away from
base decays to the burn and stops there -- `Ship_TickRangeRecover` pulls it back only while
`bAtBase`. The answer is `_ged(dist * factor, uRangeRemain)` with the budget read back AFTER the
decrement (0x7E2F4 reloads the ship slot, 0x7E300 reloads the u16). Non-zero means the craft is
further from home than its remaining range covers, which is exactly the condition
`Geoscape_TickShips` posts message 15 and pauses on, once per craft because of the `bContactMsg`
latch.

The operand order is worth stating because it reads backwards easily: `_ged` takes the scaled
distance in R0:R1 and the budget in R2:R3. That is the same order the `_gtd` at 0x37E04 in
`Tact_BuildVisRayTable` uses against its 15.0 constant, which the port already models as
`sqrt(...) > 15.0`.

`Geoscape_TickShips`' own filter in front of the call is dead code. `CMP R0,#0xFF / BNE` sends
every value except 0xFF to the call; 0xFF falls through to `CMP R0,#0xFE / BEQ`, and 0xFF is not
0xFE, so it falls through to the same call. Whatever `bHomeBase` holds, a craft away from base
reaches the gate -- which is why the gate has to bound `apBases[bHomeBase]` itself.

`Ship_FindIndexById @ 0x69ffc` is the plain half of `Ship_FindIndexByStrategType @ 0x5a92c`: the
same walk over `apCraft` comparing `Ship.bId`, but the id is an argument and there is no type or
active gate. A non-positive `Map_nCraftCount` branches straight to the `MOV R0,#0xFFFFFFFF` at
0x6A044, which is also where the exhausted loop falls through, so both answers are -1; the match
path at 0x6A050 sign-extends the index from a byte.

`Base_RemoveAllSoldiers @ 0x668e8` walks the dword list the IDB now names `g_aSoldierIdList`
(was `dword_160C90`): a count at +0 then that many ids at +4, calling `Soldier_RemoveById` per
id. The count is re-read at 0x66910 on every iteration and the loop index is compared against
that fresh value, and the return value is that re-read count rather than the soldier count --
0x66910 overwrites the quotient `Soldier_RemoveById` just returned. `BuySellScreen_BuildPanels`
zeroes the count at 0x76EAC and then walks `nSoldierCount` against the facility grid of the
selected base, and `BuySellScreen_CommitTransaction` is the only other reader; both are
unported, so the port takes the list as an input instead of guessing who fills it. That open
question is recorded in PLAN.md.

`Base_FindIndexBySiteId @ 0x5a8d0` is a second copy of the already ported
`Base_FindIndexByTypeByte @ 0x6ac30`: the same search over `BaseRec.bUnk4`, the same -1, and the
only difference is that 0x5A8D0 reads its count through `LDRSB` from `abFacilityGrid+0x618` and
bounds the loop by it, while 0x6AC30 answers -1 on an empty list and then loops with no bound at
all. One implementation covers both, and the pair is on record in renames.tsv.

`Geoscape_TickShips` no longer needs the `pfnCanInterceptReach` hook: the gate is a direct call
now, which is what the original has.

Deviations, all on the bugs.tsv ledger: the gate refuses an out-of-range `bHomeBase`, a missing
strateg item and a craft class above 4 (where the original would index past `apBases`, call
`PtrArray_Get(-1)` and multiply by stack residue read back at 0x7E160); `Ship_FindIndexById`
steps over a NULL `apCraft` slot where 0x6A024 would fault; `Base_RemoveAllSoldiers` passes the
soldier array and the running count to `Soldier_RemoveById` instead of the Map context it
ignores.

Measured: 56/56 Release, 9293 checks, 823/1103 functions (74.6%), 328948/598792 bytes (54.9%).
IDB: `Smap_CanInterceptReach` renamed to `Smap_CraftRangeExhausted`, `dword_160C90` renamed to
`g_aSoldierIdList`, comments added at 0x7df80, 0x69ffc, 0x668e8, 0x5d4e4 and 0x160C90.

The panel scroll family, the buy/sell role map and the staged stores commit (done)
----------------------------------------------------------------------------------

Ten scroll handlers, one straight line each.  ManufScreen_PanelPrev @ 0x6dd4c / PanelNext @
0x6dd94, PsiScreen_PanelPrev @ 0x715e8 / PanelNext @ 0x71648, BuySellScreen_PanelPrev @ 0x76088
/ PanelNext @ 0x760e0, BuySellScreen_DetailPanelPrev @ 0x79a70 / DetailPanelNext @ 0x79ac8 and
Transfer_ScrollPrevAll @ 0x967e4 / ScrollNextAll @ 0x9683c.  Every body is: load the panel
pointer out of its global, call the mover, repeat, and tail-call the mover one last time.  The
previous button is DecDwordIfPos_AtPlus0xBD4 @ 0x3f040 (the scroll cursor at +0xBD4 steps down
and never below zero, no wrap); the next button is IncDwordIfInRange_AtPlus0xBD4 @ 0x3f00c (it
steps up while the cursor stays below nLineCount - nStyle, +0xBDC minus +0xBE0).  No redraw, no
return value, no test on the loaded pointer.

Panel sets, in the order each handler walks them:

  ManufScreen_Panel*          pManufProductPanel @ 0x15EE44, 0x15EE48, 0x15EE40, 0x15EE38
  PsiScreen_Panel*            0x15EED0, 0x15EE88, 0x15EEA8, 0x15EE58 -- four parallel arrays,
                              one entry per base, all indexed by the SIGNED byte byte_1870F4
                              (the base the psi screen selected), so the handler picks one entry
                              out of every array for that base.  PsiScreen_ApplyAssignments @
                              0x72250 is the clearest reader: its loop is bounded by the base
                              count at 0x159D80 and it walks the selected entry's nLineCount and
                              anLineAux[] to reach the soldier record at Map+0x1198.
  BuySellScreen_Panel*        0x161A1C, 0x161A00, 0x161A20, 0x160C88, 0x1615F8
  BuySellScreen_DetailPanel*  pPlaneSlotPanel @ 0x161AC8, pPlaneSelRowPanel @ 0x161F00,
                              pPlaneCommitRowPanel @ 0x161EEC, pPlaneTakenPanel @ 0x161EF4,
                              pPlaneAvailPanel @ 0x161A40
  Transfer_Scroll*            the same five globals as BuySellScreen_Panel*, same order

BuySellScreen_PanelPrev @ 0x76088 and Transfer_ScrollPrevAll @ 0x967e4 are the same eleven
instructions over the same five globals -- a byte dump shows only the BL displacements differ --
and the next pair (0x760e0 / 0x9683c) likewise.  The port keeps one implementation of each and
cites both addresses; the merge is on record in docs/parity/renames.tsv.

Deviation: no handler tests the pointer it loads and both movers dereference +0xBD4 immediately.
The psi set is the exposed one -- an unselected base picks an array entry BuildPanels never
filled -- so the port skips a NULL slot and scrolls the rest (docs/parity/bugs.tsv, 0x6DD4C).

BuySell_RoleToItemId @ 0x76d88 maps the role byte a buy/sell row carries to the object slot that
row stands for: 0 -> 117, 1 -> 120, 2 -> 121, 3 -> 118, 4 -> 119 (the fall-through), anything
else -> 0xFF.  The argument is sign-extended from a byte first (0x76D88..0x76D8C), so a negative
role takes the 0xFF branch too.  BuySellScreen_BuildPanels reads the role with LDRSB at 0x77964
and stores the result into the row panel at +0x1380.  Only the numbers are claimed: what the
five shipped records at slots 117..121 actually say is a separate open question (see PLAN.md --
reading szName at record+0x04 right after Boot_LoadStartup drops the first few characters of
every name, which suggests the shipped name starts at record+0x08).

BuySellScreen_ApplyQtyChanges @ 0x78d90 commits the change the hangar rows staged.  The staging
array is anSlotCarryCount @ 0x161AD0 (256 dwords): BasePlaneScreen_CommitStores raises an entry
at 0x7AFD8, BasePlaneScreen_AdjustSelectedQty lowers it at 0x7AA28.  The commit walks
Map_nObjectCount @ 0x114A60 slots and moves each entry's amount between the selected base and
the selected craft, the sign saying which way:

  negative (the craft gives): BaseRec_AddItemQty @ 0x78E00 with -delta, then InvSel_SubItemQty
    @ 0x78E1C with -delta.  The amount is re-read from the array as a HALFWORD and negated a
    second time at 0x78E18 instead of reusing the first negation.
  zero or positive (the craft takes): BaseRec_SubItemQty @ 0x78E34, then InvSel_AddItemQty @
    0x78E4C.  A zero amount takes this branch and moves nothing.

The loop runs over Map_nObjectCount whatever the array's real length is, and the function ends
in a tail call to Geoscape_CloseUI(&0x1864F8) -- including when the object count is not positive,
where only the close runs.

Deviation: the base is apBases[nSelectedBaseIdx @ 0x159D81] with the SIGNED byte and no bound
(0x78DD0 LDRSB / 0x78DF0 LDR [R3,#0x1174]), so the -1 a screen leaves there reads a pointer past
the eight-slot array; the craft is apCraft[nSelectedCraftIdx @ 0x161AC4] with the raw dword,
reloaded on every iteration (0x78E18 / 0x78E44).  The port resolves both to NULL when out of
range; the two InvSel calls still run because ShipCargoApply @ 0x45E8C already refuses a NULL
owner, and the NULL base still reaches the two BaseRec calls, which is what the original does
too (docs/parity/bugs.tsv, 0x78D90).

Port: ManufScreen_PanelPrev/Next, PsiScreen_PanelPrev/Next, BuySellScreen_PanelPrev/Next,
BuySellScreen_DetailPanelPrev/Next, BuySell_RoleToItemId in src/pufo/game/pufo_baseui.c, and
BuySellScreen_ApplyQtyChanges with BuySellApplyQtyEnv in src/pufo/game/pufo_plane.c.  The port
takes the panels as arguments instead of reaching through the globals, as the earlier handlers in
this family do.  Tests: TestPanelScrollFamily and TestRoleToItemId in tests/test_baseui.c,
TestApplyQtyChanges in tests/test_plane.c.

Coverage after this unit: 834/1103 functions (75.6%), 330036/598792 bytes (55.1%).
IDB: comments added at 0x6dd4c, 0x6dd94, 0x715e8, 0x71648, 0x76088, 0x760e0, 0x79a70, 0x79ac8,
0x967e4, 0x9683c, 0x76d88, 0x78d90 and on the panel globals 0x15EE38, 0x15EE40, 0x15EE48,
0x15EE58, 0x15EE88, 0x15EEA8, 0x15EED0, 0x161A00, 0x161A1C, 0x161A20.

The alien race ids, and a wrong IDB name fixed (done)
-----------------------------------------------------

`BuySell_ItemIdToCategory @ 0x6ad30` was misnamed, and the IDB now carries the corrected name
`BuySell_ItemIdToAlienRaceId`. It does not read `ObjectRec.nCategory @ 0xB8`: that field is 0
for every slot the function maps (255 for 145..147), so it cannot be the source. What the
function answers is the alien RACE id of the item sitting in that object slot, or -1 for any
other slot. All its compares are signed (BGT / BEQ / BNE, no unsigned branch), so a negative
slot takes the -1 branch at 0x6AE38.

The body is a 25-case jump table over slots 77..101 (`SUB R0,R1,#0x4D / CMP R0,#0x18 / ADDLS
PC,PC,R0,LSL#2` at 0x6AD3C..0x6AD44) plus a chain of compares for 102, 103, 104, 114 and
144..148. Five of the compare targets land on the same MOV as a jump-table group, which is what
ties them to the same race:

  77..81 -> 9    (0x6AE3C; 148 reaches it through the CMP #0x94)
  82..86 -> 12   (0x6ADFC; 144 through the CMP #0x90)
  87..91 -> 11   (0x6AE4C; 146 through the CMP #0x92)
  92..94 -> 10   (0x6AE14; 145 through the CMP #0x91)
  95..98 -> 13   (0x6AE44; 114 through the CMP #0x72 and 147 through the CMP #0x93)
  99 -> 18, 100 -> 14, 101 -> 20, 102 -> 19, 103 -> 15, 104 -> 21
  anything else -> -1

Two independent checks pin the ids to races.

1. The shipped OBJECTS.DAT names (main.dat, 149 records of 208 bytes, name at record+0x00):
   77..81 are the five Sectoid ranks, 82..86 Snakeman, 87..91 Floater, 92..94 Muton, 95..98
   Ethereal, 99 Cyberdisk, 100 Chryssalid, 101 Reaper, 102 Silacoid, 103 Celatid, 104 Sectopod,
   114 is a sixth Ethereal rank and 144..148 are the five race medics. The groups above cut
   exactly along those boundaries.

2. The consumer. `BuySellScreen_CommitTransaction @ 0x74bf8` (not ported) switches on the
   returned id at 0x74CA0 and picks one of eleven record pointers. Those eleven are
   `Map+0x1F4..0x21C`, which is `apObjects[59..69]` -- part of the apObjects pointer array at
   `Map+0x108`, not a separate table; the IDB globals there were unnamed `unk_113BAC..unk_113BD4`
   and are now `Map_apObjects59..Map_apObjects69`. Those eleven records are the alien corpses:
   59 Sectoid, 60 Snakeman, 61 Floater, 62 Muton, 63 Ethereal, 64 Cyberdisk, 65 Chryssalid,
   66 Reaper, 67 Celatid, 68 Silacoid, 69 Sectopod. So a buy moves the item into the base and
   the matching race corpse out of it. `Smap_GrantSiteRecovery @ 0x98b50` (not ported) hard-codes
   the same pairing per slot instead of going through the race id (77..81 -> 59, 82..86 -> 60,
   87..91 -> 61, 92..94 -> 62, 95..98 and 114 -> 63, 99 -> 64, 100 -> 65, 101 -> 66, 103 -> 67,
   102 -> 68, 104 -> 69), and it agrees with the race ids slot for slot.

Port: `PufoAlienRaceId` and `BuySell_ItemIdToAlienRaceId` in src/pufo/game/pufo_baseui.c, the
mapping as a 17-entry range table. Tests: `TestAlienRaceId` in tests/test_baseui.c, 31 checks,
including that exactly 34 of the 149 shipped slots carry a race.

IDB: 0x6ad30 renamed and typed `AlienRaceId __fastcall (int pMap, int nSlotId)`; the enum
`AlienRaceId` added to the local type library; `unk_113BAC..unk_113BD4` renamed to
`Map_apObjects59..Map_apObjects69` with type `ObjectRec *`; `Map_apObjects56` re-typed from a
1-byte global to `ObjectRec *` (it is `Map+0x1E8`, i.e. the object array's 57th pointer);
comments added at 0x6ad30 and at the twelve record globals. `docs/idb/functions.tsv` and
`inventory-grouped.tsv` carry the new function name.

Coverage after this unit: 835/1103 functions (75.7%), 330328/598792 bytes (55.2%).

The object name field, and the name loop that was one slot short (done)
-----------------------------------------------------------------------

`ObjectRecFull.szName` is at record offset 0x00 and is 36 bytes. The port had `gap_0[4]` plus
`szName[32]`, which put the name four bytes late and four bytes short. Three independent proofs.

1. The shipped OBJECTS.DAT record already carries the name at +0x00: main.dat has 149 records of
   208 bytes and record 59 begins "Sectoid corpse", record 77 "Sectoid soldier", record 117
   "SKYRANGER". The IDB's `ObjectRec` types the same span as `char gap_0[36]` -- it is the name
   field, not padding.
2. `Geoscape_InitUI` writes it at offset 0. 0x86A64 is `LDR R0,[R3,#0x10C]` with R3 =
   0x1139B8 + counter*4, i.e. `apObjects[counter+1]` -- the price loop four instructions earlier
   (0x86A08) uses `[R1,#0x108]` for `apObjects[i]`, so the +4 is deliberate -- and the copy that
   follows is `STRB R2,[R0]` with no offset.
3. The counter runs 0..147 (`dword_186B08`, `CMP R0,#0x94 / BLT loc_86A44` at 0x86A90) and the
   text record is `1447 + counter` (0x86A50, `MOV R3,#0x5A0 / ORR R3,R3,#7`). With the
   destination one slot ahead, slot j is named by text record 1446+j, slot 0 is never named and
   keeps the name OBJECTS.DAT carries, and slot 148 is the last one written.

Matching the shipped data settles the off-by-one both ways. In main.dat TEXTS.DAT (1721 records,
200 bytes each behind a 2-byte marker) the name at record 1446+j equals the name the OBJECTS.DAT
record j already carries, for every slot checked. The port reads lang.dat TEXTS.DAT (1650
records) because that is the language table, and the same relation holds there: slot 59 gets
record 1505 "���� ��������", slot 77 record 1523 "������� ������", slot 117 record 1563
"��������", slot 1 record 1447 "��������".

What changed in the port:

  - `ObjectRecFull`: `char szName[36]` at 0x00, replacing `gap_0[4] + szName[32]`.
  - `Geoscape_InitUI @ 0x86A38`: the loop writes record 1447+counter into apObjects[counter+1].
  - Every `ObjectSlot` label was one slot high, because the labels were read off the 1447+slot
    mapping. The NUMBERS were always right -- they come from the original's own indexing, which
    the IDB names Map_apObjectsN -- only the names were wrong. Slot 105 is Elerium-115 (not
    "alien alloys"), 106 is alien alloys, 43 is the Heavy cannon [AP] clip (not [HE]), 68 is the
    Silacoid corpse and 69 the Sectopod remains (not the other way round), 70/71/72 are the three
    "operative corpse" slots with 73 male and 75 tank wreckage, 112 "Engineer" and 113
    "Scientist" (which the IDB's Map_apObjects112/113 already confirm), 117..121 are the five
    craft items SKYRANGER / INTERCEPTOR / FIRE STORM / LIGHTNING / AVENGER, and 122..124 the
    three soldier-equip items Personal armour / Power suit / Fly suit.
  - `Deploy_NameFromObject` (pufo_tactctl.c) and the tactical HUD item label (pufo_tacthud.c)
    read the record's first bytes; both now read `szName` instead of a raw gap.

Tests: `test_geoiinit` checks the whole mapping -- all 148 slots 1..148 carry text record
1446+slot, read back out of the loaded table -- plus that slot 0 is untouched and that szName sits
at record offset 0. `test_startnew` labels now name the items the slots really hold.

IDB: comments added at 0x86A58 and 0x86A64 spelling out the counter range, the record formula and
the apObjects[counter+1] destination, and at Map_apObjects11/31/34/56/105/112/113/121/122/123/124
naming the shipped item at each slot.

Coverage after this unit: 835/1103 functions (75.7%), 330328/598792 bytes (55.2%).

## Geoscape_UiFocusPrev / Geoscape_UiFocusNext -- the screen-id scroll dispatcher

Original: Geoscape_UiFocusPrev @ 0x8afa0 (596 B), Geoscape_UiFocusNext @ 0x8b1f4 (596 B).
Port: src/pufo/game/pufo_baseui.c, Geoscape_UiFocusPrev and Geoscape_UiFocusNext over one shared
static Geoscape_FocusScroll(pEnv, bPrev); the panel set is passed in GeoscapeFocusEnv
(src/pufo/game/pufo_baseui.h). Test: tests/test_baseui.c, TestFocusDispatcher (43 checks).

The two originals are 596-byte mirrors: an 18-case jump table on nGeoscapeScreen @ 0x186BC4
(SUB/CMP #0x11 / ADDLS PC,PC,R0,LSL#2), each case loading that screen's panel globals and
calling the mover -- DecDwordIfPos_AtPlus0xBD4 @ 0x3f040 in Prev, IncDwordIfInRange_AtPlus0xBD4
@ 0x3f00c in Next. The last mover call of every case is written as a tail call (B to the shared
loc_8B18C / loc_8B3E0 block), which is code generation, not behaviour.

Screen id -> panel set, in the order the disassembly loads them:

| id | panels |
|----|--------|
| 0, 2 | none: Prev stores 1 to unk_1864F4, Next stores 1 to unk_1864F6 |
| 4 | pSolPanelNames 0x15975C, pSolPanelCraft 0x159DEC |
| 6 | pPlaneSlotPanel 0x161AC8, pPlaneSelRowPanel 0x161F00, pPlaneCommitRowPanel 0x161EEC, pPlaneTakenPanel 0x161EF4, pPlaneAvailPanel 0x161A40 -- only while bPlaneStoresCommitted @ 0x161F28; then pPlaneCrewPanel 0x161EDC, pPlaneCrewStatusPanel 0x161A3C -- only while bPlaneListPanelsBuilt @ 0x161F24 |
| 7 | pBaseFacPanel 0x15886C |
| 8 | pResearchTopicPanel 0x160C68, pResearchPanelB 0x15EEFC, pResearchPanelC 0x15EF00 |
| 9 | pManufProductPanel 0x15EE44, pManufPanelB 0x15EE48, pManufPanelC 0x15EE40, pManufPanelD 0x15EE38 |
| 10, 11, 12 | pBuySellPanelA 0x161A1C, pBuySellPanelB 0x161A00, pBuySellPanelC 0x161A20, pBuySellPanelD 0x160C88, pBuySellPanelE 0x1615F8 |
| 16 | unk_187200 |
| 17 | the four psi arrays (unk_15EED0, byte_15EE84+4, byte_15EE84+0x24, unk_15EE58), each indexed by the signed byte at byte_1870F4 |
| 1, 3, 5, 13, 14, 15 | nothing |

Screens 10, 11 and 12 -- transfer, buy and sell -- share one panel set, which is the same fact
the merged Transfer_ScrollPrevAll / ScrollNextAll pair records. Screen 6 is the only case with
two gates: the five store panels are skipped when bPlaneStoresCommitted is clear, and the crew
pair is skipped when bPlaneListPanelsBuilt is clear, so a hangar that has not built its lists
scrolls nothing past the five.

Flag-byte quirk, kept as-is: for screens 0 and 2 the value the handler returns is the address of
unk_1864F4 in BOTH functions, but Next writes unk_1864F6 (STRB R1,[R0,#2]). The two bytes are
also written by Geoscape_SetPanelFlagA/B @ 0x8b448 / 0x8b478 and read by Geoscape_PulseUpdate
(0x954dc, not ported); their meaning is not established by these two functions.

Deviations:
- The originals return a pointer (the mover's result, or &unk_1864F4 for the 0/2 case). Both
  callers, Input_OnUp @ 0x118b0 and Input_OnDown @ 0x119b0, discard it, so the port returns void.
- The originals load each panel global and call the mover without checking it. The port skips a
  NULL slot, the same deviation already recorded for 0x6DD4C.
- The port takes the four psi panels already resolved for the base that byte_1870F4 selects; the
  original re-reads the byte before each of the four loads. Nothing writes it in between, so the
  two are equivalent.
- A NULL GeoscapeFocusEnv does nothing; the original has no such case.

IDB: the ten panel globals named above were previously unk_ and are now typed UiPanel * with the
names the table uses (0x15EE38, 0x15EE40, 0x15EE48, 0x15EEFC, 0x15EF00, 0x160C88, 0x1615F8,
0x161A00, 0x161A1C, 0x161A20). Function comments added at 0x8afa0 and 0x8b1f4 recording the
screen -> panel-set map and the flag-byte quirk.

## BuySellScreen_BuildPanels -- the buy/sell list panels

Original: BuySellScreen_BuildPanels @ 0x76de0 (4236 B).
Port: src/pufo/game/pufo_baseui.c, `BuySellScreen_BuildPanels(BuySellBuildEnv *)`, with
`g_aBuySellCtlDesc[4]` and `g_aBuySellRowOrder[148]` beside it. Test: tests/test_baseui.c,
`TestBuySellBuildPanels` (33 checks).

The branch selector is the byte at 0x161A28, now named `bBuySellPanelsBuilt` in the IDB;
BuySellScreen_Draw (0x77e6c, not ported) reads the same byte.

Teardown (0x77D60..0x77E50) releases the four controls at 0x161A0C..0x161A18 and the five panels
in the order A, B, E, C, D, and then restores `nFunds` from `BuySell_nFundsSnapshot` @ 0x161460
when they differ. That is the rollback of a transaction the player did not confirm -- the screen
edits nFunds directly while the quantities move.

Build. Four `UiControl` objects (new 0x20 + UiControl_ctor) into 0x161A0C..0x161A18; the first two
get `pfnClick = BuySellScreen_PanelPrev / BuySellScreen_PanelNext` before any Setup runs; all four
are then `UiControl_Setup(ctl, 0, 0, stru_ABBA0[i])` -- "up" at (205,18), "down" at (220,38),
"bclose" at (0,1), "emptyb" at (62,297). `dword_161468[0]` (the assigned-ship count) and
`g_aSoldierIdList[0]` (the scratch id list Base_RemoveAllSoldiers @ 0x668e8 consumes) are cleared.

Four counting passes share one row total:

| rows | gate |
|---|---|
| soldiers | `Soldier.nKind == base` and `Soldier.bUnk183 == 255` (no craft) |
| craft | `Ship.bHomeBase == base`, `Craft_CountSoldiersAboard == 0`, `Ship.bAtBase != 0` |
| objects | slots 1..69, 105..111, 122..124, 142, 143 with `ObjectRecFull.auPerBaseVal[base] != 0`; slot 112 when `Base_GetEngineerCount > 0`; slot 113 when `Base_GetScientistCount > 0` |
| aliens | slots 77..104, 144..148, 114 with `auPerBaseVal[base] != 0` |

Five panels, each `new 0x1BB4` + `UiPanel_ctor(row total)`, nStyle 13, nFont 16:

| panel | global | pos | click | width |
|---|---|---|---|---|
| A, the name | pBuySellPanelA 0x161A1C | 10,70 | BuySellScreen_SyncSelection | 125 |
| B, the "<" column | pBuySellPanelB 0x161A00 | 164,70 | BuySellScreen_FillBuyPanel (0x75160, not ported) | 15 |
| C, the ">" column | pBuySellPanelC 0x161A20 | 180,70 | BuySellScreen_FillSellPanel (0x75464, not ported) | 15 |
| D, the available count | pBuySellPanelD 0x160C88 | 158,70 | none | 50 |
| E, the pending quantity | pBuySellPanelE 0x1615F8 | 232,70 | none | 45 |

Panel A's `anLineAux` is the row-kind marker, and this is what the deferred
BuySellScreen_CommitTransaction (0x74bf8) reads: 1000 for a soldier, `BuySell_RoleToItemId`
(117..121) for a craft, the slot id for a plain object, and slot + 2000 for an alien. Panel B's
aux is the soldier's index into `apSoldiers` (soldier rows only), panel C's is the soldier's roster
id at `Soldier + 0x1B8` or `Ship.bId` on a craft row, panel D's is the available count (1 for a
soldier or a craft, `auPerBaseVal[base]` for an object, the crew count for slots 112 / 113), and
panel E's is always 0 here -- the pending quantity the transaction then edits.

Craft rows are named `TEXTS[415 + Ship.bCraftType] + "-" + itoa(Ship.bType)`. Records 415..419 are
Skyranger, Lightning, Avenger, Interceptor, Fire Storm -- one per CraftType, the same role
`BuySell_RoleToItemId` maps to object slots 117..121. Verified against both shipped archives:
main.dat gives the English names, lang.dat the Russian ones.

The sort pass walks `g_aBuySellRowOrder` @ 0xAAA04 (148 dwords, read up to `nObjectCount - 1` of
them) and for each value swaps the matching panel A row into the next free position in all five
panels. Two things fall out of it. First, the table lists 1000, 112, 113, 117..121, the plain
object slots and the alien slot numbers 77..104 / 144..148 / 114 -- but alien rows carry slot +
2000, so those 34 entries never match and the aliens keep their fill order at the end. That is the
original's own inconsistency, reproduced rather than corrected. Second, the inner scan does not
stop at the match, so two rows sharing an aux value (two soldiers, both 1000) each advance the
placement cursor; the second swap is a self-swap.

Deviations:
- The name buffer at 0x77730 is `char v106[17]` followed by `char v107`, and the copy loop also
  copies the terminator, so a name of 18 characters or more writes one byte past the buffer before
  the `strlen > 0x17` test cuts it to 17. The port bounds the copy; the 17-character cut is kept.
- The original dereferences `apSoldiers[i]`, `apCrafts[i]` and `apObjects[i]` without a null test;
  the port skips a null slot.
- The original calls `Base_GetEngineerCount` / `Base_GetScientistCount` again in the fill pass; the
  port reuses the values the count pass computed. Nothing writes the crew columns in between, so
  the two are the same number.
- The four controls and the five panels are `operator new`'d by the original and deleted by its
  teardown. The port takes the controls as caller-owned (only their pads are released) and owns the
  panels itself, matching the pattern the other screen builders in this file use.
- The port bounds the slot include table at 255 entries and the order table at 148; the original
  indexes both blindly.

IDB: `unk_161A28` named `bBuySellPanelsBuilt` (byte), `unk_161A0C` named `g_aBuySellCtl`
(`UiControl *[4]`), `unk_AAA04` named `g_aBuySellRowOrder` (`int[148]`), and the enum
`PufoBuySellRowMarker` (`BUYSELL_ROW_SOLDIER = 1000`, `BUYSELL_ROW_ALIEN_BIAS = 2000`) added. A
function comment at 0x76de0 records the gate table and the aux encoding.

## BuySellScreen_CommitTransaction -- confirming the buy/sell screen

Original: BuySellScreen_CommitTransaction @ 0x74bf8 (1176 B), installed as the confirm control's
handler for screen 12 by BuySellScreen_Init (0x78c30, not ported; screen 11 gets
BuySellScreen_ConfirmSiteAndClose instead).
Port: src/pufo/game/pufo_baseui.c, `BuySellScreen_CommitTransaction(BuySellCommitEnv *)`, with
`BuySellCorpseSlotForRace` beside it. Test: tests/test_baseui.c, `TestBuySellCommitTransaction`
(17 checks).

Six phases.

1. 0x74C0C..0x74D74. For every slot 0..nObjectCount-1, `BaseRec_SubItemQty(base, apObjects[slot],
   -g_anBuySellSlotDelta[slot])` -- the IDB now names the array `g_anBuySellSlotDelta` @ 0x161600,
   cleared over `Map_nObjectCount` entries by BuySellScreen_Init. That is `auPerBaseVal[base] +=
   delta` and `nStoresTotal += delta`. If the slot has an alien race, the race's corpse record takes
   `-delta` through `BaseRec_AddItemQty`, so buying aliens moves corpses the other way. The corpse
   pairing is the switch at 0x74CA0: 59 Sectoid, 60 Snakeman, 61 Floater, 62 Muton, 63 Ethereal,
   64 Cyberdisk, 65 Chryssalid, 66 Reaper, 67 Celatid, 68 Silacoid, 69 Sectopod -- the same pairing
   Smap_GrantSiteRecovery (0x98b50, not ported) hard-codes per slot.

2. 0x74D88..0x74E64. Panel A rows whose aux is 117..121 (a craft) and whose panel E aux is 1 (the
   selection flag BuildPanels zeroes and the row handlers set) contribute
   `Ship_FindIndexById(panelC aux)` -- panel C holds `Ship.bId` on a craft row -- to the assigned-ship
   list at dword_161468[1..], count in [0].

3. 0x74E70..0x74F14. For each of those craft indices and every object slot,
   `apObjects[slot]->aItemQty[craftIndex]` is added to the base and subtracted from the craft, so
   confirming unloads every selected craft. Note the two indices differ in kind: the read uses the
   array index from Ship_FindIndexById, `InvSel_SubItemQty` writes through `Ship.nSlotIdx` (Ship +
   0x1C, the hangar slot). They agree whenever a craft's array index is its hangar slot.

4. 0x74F38..0x74FB0. Panel A rows whose aux is 1000 (a soldier) AND whose panel E aux is 1 fill
   `g_aSoldierIdList[1..]` with panel C aux -- the soldier's roster id at `Soldier + 0x1B8`. Only
   soldier rows qualify; an unselected soldier row is skipped.

5. 0x74FB8..0x75064. For each roster id, the soldier whose `nUnk440` matches gives their equipped
   armour back to the base: `apObjects[121 + Soldier.nUnitType]` +1. `Soldier.nUnitType` at +0xA0 is
   the same byte SoldierScreen_EquipSelected writes, with 0 meaning nothing worn.

6. 0x75070..0x75078. `BuySell_nFundsSnapshot = nFunds` -- the value BuildPanels' teardown restores
   when the player leaves without confirming -- and a tail call to Geoscape_CloseUI (0x8b94c, not
   ported) on the screen block at 0x1864F8, the same block whose two flag bytes
   Geoscape_UiFocusPrev/Next set.

This is what the deferred unit was waiting on: the aux encoding is now pinned from both ends --
BuildPanels writes it, CommitTransaction reads it back.

Deviations:
- The original indexes `apObjects[121 + Soldier.nUnitType]` without bounding the byte and
  `aItemQty[craftIndex]` with an unchecked index; the port bounds the armour index to 1..3 and
  treats an out-of-range cargo column as zero.
- The original writes the assigned-ship and soldier-id lists without a capacity check; the port
  counts every match but stores only within the caller's array.
- Null panel and array slots are skipped; the original dereferences them.

IDB: `unk_161600` named `g_anBuySellSlotDelta` (`int[149]`), function comments at 0x74bf8 (the six
phases) and 0x78c30 (the screen 11 / 12 wiring and the delta clear).
## BuySellScreen_FillSellPanel / BuySellScreen_FillBuyPanel -- the buy/sell row editors

Port: `BuySellScreen_FillSellPanel` and `BuySellScreen_FillBuyPanel` in src/pufo/game/pufo_baseui.c,
plus the shared `BuySellRepeatLimit`, `BuySellRowDelta`, `BuySellRowDeltaAdd` and `BuySellSlotRec`
helpers and the six meta-colour macros in pufo_baseui.h. Evidence: 0x75464 (1044 bytes) and 0x75160
(772 bytes) -- the click handlers BuySellScreen_BuildPanels wires onto panel C at 0x774EC and onto
panel B at 0x773D0. Test: `TestBuySellRowEditors` in tests/test_baseui.c, 54 checks.

Each handler reads the row from the clicked panel's own cursor at +0xBD8 (`nUnk3032`) and takes the
row kind from `pBuySellPanelA.anLineAux[nRow]`, the encoding BuildPanels writes:

1. aux == 1000 (a soldier) and 117..121 (a craft) share one body. Sell side, 0x754A8 and 0x7558C:
   guard `pBuySellPanelD.anLineAux[nRow] > 0`, then panel E meta (100,255,100 / 10,50,10), panel E
   aux = 1, panel D aux -= 1. Note the assignment: 0x755F4 is `MOV R3,#1` then `STR`, so panel E is
   a selection FLAG on these rows, not a count -- a second click leaves it at 1 while the count
   column keeps dropping. That is the flag BuySellScreen_CommitTransaction reads back at 0x74D88 and
   0x74F38. Buy side, 0x7519C and 0x75278: guard panel E aux > 0, meta (240,240,240 / 20,20,20),
   panel E aux = 0, panel D aux += 1.

2. aux > 2000 (an alien row, object slot + 2000). Sell, 0x75514: guard panel D aux > 0, then
   `g_anBuySellSlotDelta[aux - 2000] -= 1`, panel E aux += 1, panel D aux -= 1. Buy, 0x751F4: guard
   panel E aux > 0, `delta[aux - 2000] += 1`, panel E aux -= 1, panel D aux += 1. Here panel E really
   is a count -- the marked-unit total -- because the quantity itself lives in the delta array.

3. Any other aux is a plain object slot, and the repeat loop runs. Sell, 0x75670..0x7586C: while
   `delta[slot] + Base_GetItemQty(rec, nSelectedBaseIdx) > 0`, take one off the count column and off
   the delta, and add `ObjectRecFull.uPrice` to `BuySell_nTransTotal` (0x161A2C), to `nFunds` and to
   panel E aux -- the original credits the sale price to the funds straight away, before the commit.
   Slots 112 and 113 (engineer / scientist) re-test `Base_GetEngineerCount` / `Base_GetScientistCount`
   before spending, then write panel E aux = -delta[slot] instead of accumulating, and move no money.
   Buy, 0x7533C..0x75458: the mirror -- panel D aux += 1, delta += 1, and the price comes back out of
   the total, out of the funds and off panel E aux.

The loop bound is the finding worth recording. Both loops compute their iteration count as
`nRadarPulseTimer / 10` (0xAC224) through `_rt_sdiv` and re-load the global on every pass (0x75630,
0x75440). 0xAC224 is the radar screen's pulse countdown -- the radar click leaves 250 in it and
RadarScreen_DrawRangeBar draws it -- and nothing in the buy/sell screen owns or initialises it, so
one click of ">" moves anywhere between one and 26 units depending on a timer from another screen.

The two loops are also not symmetric, which is a second original defect: the sell guard is a branch
to the loop exit (0x75694 `BMI loc_75870`), the buy guard only skips the body (0x75350, a branch to
the loop tail) and lets the loop keep turning, and the buy side's `panel E aux -= price` (0x753D8)
has no floor, so a row whose pending money is below one unit's price ends up negative and can drag
`BuySell_nTransTotal` negative with it. Both are recorded in docs/parity/bugs.tsv.

Deviations:
- Neither handler tests the row cursor; the port bounds it against panel A's line count.
- The original dereferences all five panel globals and the object array blindly; the port returns
  when any is missing and treats a missing record's price as zero.
- The port bounds the delta array to `nSlotDeltaCount`; 0x75548 and 0x7538C index
  `g_anBuySellSlotDelta[aux - 2000]` with whatever aux holds.

IDB: `unk_161A2C` named `BuySell_nTransTotal`; function comments at 0x75464 and 0x75160 giving the
row-kind branches, the two meta colour triples and the timer-bound loop.
## BuySellScreen_ValidateCapacity / BuySellScreen_UpdatePrices -- the buy screen's quantity controls

Port: `BuySellScreen_ValidateCapacity` and `BuySellScreen_UpdatePrices` in
src/pufo/game/pufo_baseui.c, with `BuySellCapacityEnv`, the `PufoBuySellRowMarker` and
`PufoBuySellShortfall` enums and the three price macros in pufo_baseui.h. Evidence: 0x75b88
(1280 bytes) and 0x75878 (784 bytes) -- the click handlers BuySellScreen_BuildControls installs into
the panel click slot at +0xBCC: UpdatePrices onto pBuySellPanelB (0x765CC) and ValidateCapacity onto
pBuySellPanelC (0x766EC). Test: `TestBuySellCapacityGate` in tests/test_baseui.c, 61 checks.

Both read the selected row from the cached index at unk_186BE8 and take the row kind from
`pBuySellPanelA.anLineAux[nRow]`, the same marker BuySellScreen_BuildPanels writes, and both repeat
`nRadarPulseTimer / 10 + 1` times -- the radar screen's pulse countdown again, so the defect already
recorded for 0x75160 and 0x75464 applies here too.

Everything they change lands on the transfer flow's working site, `aSmapSites[nSiteCount]`: the
record AT the site count, the slot Transfer_ResetSite (0x9690c, not ported) clears and
BuySellScreen_ConfirmSiteAndClose commits. The ARM builds the address as `0x162030 + 2*(371*count +
slot) + 2` (0x75D88: `MOV R3,#0x170; ORR R3,R3,#3` for 371, `MUL` by the count), which is the u16
column of a 742-byte site record.

ValidateCapacity buys one unit per pass, and the capacity test depends on the row kind:

1. 117 and 118, the two craft products (0x75C28..0x75C84). Budget `Base_CalcHangarCap`, pressure
   `Base_CountShipsAtBase + Base_CountInboundTransfers + 1`. Over it: shortfall 2. Note that
   Base_CountInboundTransfers is called with the site count as the site index, so the pending craft
   already queued on the working site counts against the hangar -- which is why a second pass in the
   same click fails even though nothing else changed.
2. Every other product row (0x75C88..0x75D10). Budget `Base_CalcStoresCapacity`, pressure
   `Base_CountItemQtyAcrossBases + ObjectRecFull.bCargoSize (0xC6) + Base_CalcStorageUsed`. Over it:
   shortfall 3.
3. Then, for any product row, `uPrice (0xBC) > (unsigned)nFunds` (0x75D18, `CMP`/`BHI`): shortfall 1.
4. The pass itself: 117/118 also bump the site's signed `bUnk518` (0x206, `LDRSB`/`STRB` at
   0x75D54); panel D's `anLineAux[nRow] += 1`; `site.auItemQty[nAux] += 1`; `nFunds -= uPrice`;
   `BuySell_nTransTotal += uPrice`.
5. 1000 / 112 / 113 take the other branch (0x75E08..0x75E64). Budget `Base_CalcLivingQuartersCap`,
   pressure `Base_CalcOccupiedSpace + Base_CountOutboundTransferSpace + 1`. Over it: shortfall 0.
   Then the fixed prices -- 40000 a soldier (0x9C40), 50000 an engineer (0xC350), 60000 a scientist
   (0xEA60) -- against nFunds, shortfall 1. A soldier bumps nSoldierHireCount (0x159D90) and the
   site's `uRecoveredSoldiers` (0x202) and the pass continues; an engineer bumps `auItemQty[112]` and
   a scientist `auItemQty[113]`, and both `return` out of the whole function (0x76008, 0x7607C), so
   one click queues at most one crew member however long the loop is.

UpdatePrices is the mirror and sells one unit per pass with no capacity test at all: panel D's count
is the only guard, and a zero count skips the body without leaving the loop. 1000 gives back 40000
and decrements nSoldierHireCount and `uRecoveredSoldiers`; 112 and 113 give back 50000 and 60000,
decrement `auItemQty[112]` / `[113]` and return; any other row decrements panel D, decrements the
signed `bUnk518` for 117/118, decrements `auItemQty[nAux]` and moves the price back.

The shortfall flag byte_1870C8 is written in three shapes, and the difference is real: the
living-quarters path (0x75F98) and the crew-price path (0x75F74) store 1 unconditionally, the hangar
and stores paths (0x75F34, 0x75F54) store 1 only when it is still clear, and the product-price path
(0x75F64) stores 1 only when clear and then uses the flag's own value as the reason code -- 1 either
way, because nothing in the program writes a different value there. The reason always lands in
dword_1870B4[0]: 0 living quarters, 1 funds, 2 hangar, 3 stores.

Deviations:
- The original bounds nothing. It indexes `aSmapSites[nSiteCount]`, `apObjects[nAux]` and the panel
  arrays blindly, so an alien row (aux = slot + 2000) would write `auItemQty` past the 256 columns of
  the site record; the port bounds the row, the slot and the site.
- The original's return value is a residue -- the site pointer, the loop limit, or `&dword_1870B4` --
  that no caller reads; the port returns the shortfall code, or -1 when every pass completed.
- Missing panels, cursor, funds and total pointers are skipped; the original dereferences them.

IDB: `unk_159D90` named `nSoldierHireCount` (typed `int`, with the observed use recorded in a comment),
function comments at 0x75b88 and 0x75878. The `PufoBuySellRowMarker` enum gained PUFO_ROW_ENGINEER,
PUFO_ROW_SCIENTIST, PUFO_ROW_CRAFT_FIRST and PUFO_ROW_CRAFT_SECOND; its two older members stay named
BUYSELL_ROW_SOLDIER and BUYSELL_ROW_ALIEN_BIAS because enum members are not globals and the rename
tool cannot retitle them.
## BuySellScreen_Init -- the buy/sell screen's bring-up

Port: `BuySellScreen_Init` with `BuySellInitEnv` and the pad / palette / screen-id macros in
src/pufo/game/pufo_baseui.{c,h}. Evidence: 0x78c30 (352 bytes), the tail call of
BuySellScreen_OpenBuy @ 0x61794 (the BL at 0x617B8) and of BuySellScreen_OpenSell @ 0x617c4
(0x617E8); both openers write the screen id first -- 11 for buy, 12 for sell -- and then call this
with the screen slot at pBuySellScreenPad @ 0x161A08. Test: `TestBuySellInit` in tests/test_baseui.c, 25 checks.

In the original's order:

1. 0x78C44: `BuySell_nFundsSnapshot = nFunds`. The snapshot BuildPanels' teardown restores is taken
   here at the open as well, not only at the commit (0x75070).
2. 0x78C4C..0x78C70: `operator new(0x14)` then `PadImage_ctor`, and the slot receives the ctor's
   result -- the `if (v2) v3 = ctor(v2); else v3 = 0;` shape, not the raw allocation.
3. 0x78C7C, 0x78C8C: `Palette_Load("buy_sell.pal", 5)` then `PadImage_Load(pad, "buy_sell", 5)`, and
   the pad's `nOriginX` / `nOriginY` are cleared at 0x78C98 and 0x78CA4. The palette slot is the same
   5 the pad's mode byte takes, so the pad draws with its own palette.
4. 0x78CAC..0x78CB4: `BuySell_nTransTotal` and `nSoldierHireCount` (0x159D90) are cleared. This is
   the third function that proves 0x159D90 belongs to the buy/sell screen: Init clears it,
   ValidateCapacity increments it per soldier bought, UpdatePrices decrements it per soldier sold, and
   Save_WriteBases writes it as a 4-byte field of the bases chunk at 0x3F28C.
5. 0x78CBC..0x78CD4: the close control unk_186B00 goes to x 224, y 1 -- two `STRH`, so 16-bit stores
   into a 32-byte UiControl -- with BuySellScreen_CloseThunk @ 0x75154 as its click.
6. 0x78CE8..0x78CF8: the confirm control unk_186BA4 goes to `x = 120 - (uWidth >> 1)` (`LDRH`,
   `LSR #1`, `RSB #0x78`) and y 297 (`0x128` built as `ORR #1`). The same centring
   BaseAtScreen_Open @ 0x588cc does, and the same control slot the hangar screen borrows.
7. 0x78D08..0x78D44: `g_anBuySellSlotDelta` is cleared over `Map_nObjectCount` entries -- the
   `LDRH [0x1139B8, #0x12A0]` at 0x78D08, then the post-increment store loop at 0x78D3C.
8. 0x78D4C..0x78D7C: by the screen id. 11 installs BuySellScreen_ConfirmSiteAndClose @ 0x75090 on the
   confirm control and calls BuySellScreen_BuildControls @ 0x76160; 12 installs
   BuySellScreen_CommitTransaction @ 0x74bf8 and calls BuySellScreen_BuildPanels @ 0x76de0. The 12
   branch is written with `LDREQ` / `STREQ` / `BLEQ`, so the handler store and the call are both
   conditional on the id being read a second time -- the id cannot change in between, so the port
   reads it once.

Deviation: the original dereferences the allocation and both control globals with no test, and
`PadImage_Load` is called on the slot's value even when the allocation failed; the port skips what
failed to allocate or was not supplied, and zero-fills the pad the original leaves partly
uninitialised.

IDB: the function comment at 0x78c30 rewritten against the current names (`BuySell_nTransTotal`,
`nSoldierHireCount`), which the older comment still spelled `unk_161A2C` and `unk_159D90`.


## BuySellScreen_BuildControls @ 0x76160 -- the buy screen's object block

Evidence: `BuySellScreen_BuildControls @ 0x76160` (3112 B), the call the screen-11 branch of
`BuySellScreen_Init @ 0x78c30` makes. It is the buy mode's counterpart of
`BuySellScreen_BuildPanels @ 0x76de0`: the same five panel globals, the same four `g_aBuySellCtl`
controls and `stru_ABBA0` descriptors, the same `g_aBuySellRowOrder` sort, the same funds rollback --
but a different row set (the shop's catalogue, not what the base owns), a different geometry,
`nStyle` 14 instead of 13, and the record it writes is the transfer flow's working site. Test:
`TestBuySellBuildControls` in tests/test_baseui.c, 51 checks.

The built flag is `bBuySellControlsBuilt @ 0x161A24` (IDB, was `unk_161A24`), a sibling of
`bBuySellPanelsBuilt @ 0x161A28`: each mode sets its own and each mode's teardown keys off its own.

Teardown branch, `bBuySellControlsBuilt != 0` (0x76C08..0x76D70):

1. 0x76C08..0x76C7C: the working site `aSmapSites[nSiteCount]` is reset whole -- `auItemQty[0..255]`,
   `bFlag0`, `bFlag1`, `uRecoveredSoldiers`, `bRecoveredSoldierAssign`, `bRecoveredCraftCount`,
   `anSoldierIds[0..99]` and `anCraftIds[0..9]`. `bUnk518` is NOT cleared here; only the build branch
   writes it, at 0x761DC. This is the same record `Transfer_ResetSite` (0x9690c, not ported) clears.
2. 0x76C80: the flag is cleared. 0x76C84..0x76CB8: the loop starts at `&pBuySellScreenPad` and reads
   `v69[1]`, so it walks the four `g_aBuySellCtl` slots at 0x161A0C..0x161A18 and skips the pad the
   slot itself holds; each control is `UiControl_FreePads`'d and `operator delete`'d.
3. 0x76CC4..0x76D58: panels A, B, E, C, D are freed in that order (`UiPanel_FreeLines` then delete),
   the same order BuildPanels' teardown uses at 0x77CC4..0x77E30.
4. 0x76D70: `if (BuySell_nFundsSnapshot != nFunds) nFunds = BuySell_nFundsSnapshot`, the same
   uncommitted-transaction rollback BuildPanels' teardown does.

Build branch (0x7619C..0x76BE8):

1. 0x761A8..0x7620C: the working site is opened -- `bFlag0 = 8`, `bFlag1 =
   apBases[nSelectedBaseIdx]->bUnk4` (the base's site type, which is what
   `Base_CountInboundTransfers` and `Base_CountOutboundTransferSpace` match sites against),
   `bUnk518 = 0`, `uRecoveredSoldiers = 0`, `auItemQty[0..255] = 0`. The two id lists and
   `bRecoveredCraftCount` are left alone here -- the build branch does not clear what the teardown
   branch clears.
2. 0x76218..0x76298: four controls (`operator new(0x20)` + `UiControl_ctor`) into `g_aBuySellCtl`; [0]
   gets `BuySellScreen_PanelPrev @ 0x76088` and [1] `BuySellScreen_PanelNext @ 0x760e0`; all four get
   `UiControl_Setup(ctl, 0, 0, stru_ABBA0[i])` -- the same four descriptors BuildPanels uses.
3. 0x762A4: the flag is set, before the include pass.
4. 0x762A8..0x76334: the include table, a stack dword array indexed by slot (`v79[4*i+104]`). A slot
   gets a row when it is 1..58, 112, 113, 117 or 118 AND `ObjectRecFull.uPrice` (0xBC) is non-zero AND
   the u16 at +0xBA is zero. 0x76318 (`LDRH R2,[R1,#0xBA]`) is the only read of that halfword in the
   whole binary. In the shipped OBJECTS.DAT the second test excludes nothing the first has not already
   excluded -- all 35 records with a non-zero +0xBA have no price -- and 36 slots pass: 1, 4, 7, 8, 10,
   14..17, 19, 22, 25..27, 31, 32, 36, 38, 40..48, 52..54, 56, 57, 112, 113, 117, 118.
5. 0x76344: `nLineCount = included + 1`, the extra row being the soldier the shop sells.
6. 0x76348..0x768B4: the five panels, all `nStyle` 14 and `nFont` 16, at x 10 / 174 / 180 / 196 / 232
   with `nViewW` 125 / 50 / 15 / 15 / 45 and `nUnk7052` 5 / 130 / 176 / 192 / 190. Panel A's click is
   `BuySellScreen_SyncSelection @ 0x76138`, panel B's `BuySellScreen_UpdatePrices @ 0x75878` and panel
   C's `BuySellScreen_ValidateCapacity @ 0x75b88` -- the two handlers of the previous unit, now
   installed by the code that reads their row data. Panels B and C get `SetBytes3to5(0x32, 0x32, 0xFA)`
   and `nUnk7084 = 5`; A, E and D do not.
7. 0x768DC..0x76AC4: the fill pass, in slot order, only where the include flag is 1. Panel A gets the
   record name through the same 17-character cut BuildPanels uses and aux the slot id, panels B and C
   the `aRowQtyDec` ("<" at 0xABC24) and `aRowQtyInc` (">" at 0xABC20) literals, panel E aux the price
   and panel D aux zero. Inside the slot-113 branch an extra row is written: panel A's text is
   `aTextRecs @ 0xB38D0 + 205*159` = 0xBB823 = TEXTS[159] ("Soldier" in main.dat, Russian in lang.dat),
   panel A aux 1000 -- the one row marker that is not an object slot -- panel E aux 40000 and panel D
   aux 0. 40000 is exactly what `BuySellScreen_ValidateCapacity` charges for a soldier, so the price
   column and the capacity gate agree.
8. 0x76AF0..0x76BE8: the selection sort, `nObjectCount - 1` passes over `g_aBuySellRowOrder`, swapping
   panels A, E, B, C and D together -- byte-for-byte the pass BuildPanels ends with.

Deviation: the original allocates the four controls with `operator new` and `operator delete`s them in
its teardown, and dereferences every allocation and both panel and control globals with no test; the
port takes caller-owned controls and frees only the panels it allocated. The row count is
`included + 1` unconditionally while the soldier row is written only inside the slot-113 branch, so a
data set where slot 113 has no price leaves one counted row as `UiPanel_ctor` left it (empty text, aux
0); the shipped OBJECTS.DAT prices slot 113 at 60000, so this does not arise there.

IDB: the function comment at 0x76160 rewritten against the current names; a comment at 0x76318 naming
the +0xBA read; `unk_161A24` -> `bBuySellControlsBuilt` (typed `unsigned __int8`), `unk_161A08` ->
`pBuySellScreenPad`, `unk_ABC20` -> `aRowQtyInc` (">"), `unk_ABC24` -> `aRowQtyDec` ("<") -- the two
literals are shared by four list builders (0x76160, 0x76de0, 0x7b014, 0x96b30); a comment at 0xBB823
recording it as TEXTS[159].


## Ui_InitTransfer @ 0x9955c and Transfer_ResetSite @ 0x9690c -- the transfer screen's bring-up

Evidence: `Ui_InitTransfer @ 0x9955c` (392 B), the tail call of `TransferScreen_Open @ 0x614fc` (the
BL at 0x61510, with the screen's root array at 0x1870D8), and `Transfer_ResetSite @ 0x9690c` (420 B),
the third of the four controls it wires. `Transfer_RoleToIconId @ 0x96ad8` is the third function in
this unit and it is `BuySell_RoleToItemId @ 0x76d88` byte for byte -- same 88 bytes, same jump table,
same 255 fall-through -- so the transfer screen shares that one port (docs/parity/renames.tsv). Test:
`TestTransferInitAndReset` in tests/test_baseui.c, 55 checks.

Ui_InitTransfer, in the original's order:

1. 0x99568..0x9958C: `operator new(0x14)` then `PadImage_ctor`, and the slot receives the ctor's
   result.
2. 0x99590: `PadImage_Load(pad, "transfer", 13)` -- `aTransfer @ 0xAC438`, palette slot 13, the same
   "load the pad named after the screen in the screen's own palette slot" shape BuySellScreen_Init
   uses with "buy_sell" at slot 5. The origin is cleared at 0x99598 and 0x995A0.
3. 0x995A4: `TransferScreen_BuildPanels` (0x96b30, not ported) runs before the controls exist.
4. 0x995B8: `byte_1870F8 = 1`. This is the same byte BaseInfoScreen_OnOk (0x557C8) and
   PsiScreen_OnOk (0x716A8) set when a base change is accepted; here it means "this transfer is still
   possible", and Transfer_ResetSite is the function that clears it.
5. 0x995C0..0x995F0: four controls (`operator new(0x20)` + `UiControl_ctor`) into ppRoot[1..4].
6. 0x99604..0x99630: their clicks in slot order -- `Transfer_ScrollPrevAll @ 0x967e4` and
   `Transfer_ScrollNextAll @ 0x9683c`, both already merged into BuySellScreen_PanelPrev /
   BuySellScreen_PanelNext, then Transfer_ResetSite and TransferScreen_CommitTransfer (0x97aa4, not
   ported).
7. 0x9963C..0x99654: the close control unk_186B00 goes to x 224, y 1 with
   `Geoscape_CloseUI_ThunkB @ 0x96020`. Same slot as the buy/sell screen, but a different handler:
   BuySellScreen_Init installs BuySellScreen_CloseThunk @ 0x75154 there.
8. 0x9965C: the confirm control unk_186BA4 gets that same close thunk, and it gets it *before*
   `UiControl_Setup`, where BuySellScreen_Init installs its confirm handler last. The transfer
   screen's confirm button therefore closes the screen; its commit lives on the fourth control.
9. 0x99660..0x99680: `UiControl_Setup(ctl, 0, 0, g_aTransferCtlDesc[i])` (was `stru_ABBA0`'s
   counterpart `stru_AC3B8 @ 0xAC3B8`, now named in the IDB). The descriptors are (0xCD, 0x12) "up" /
   "sup", (0xCD, 0x26) "down" / "sdown", (1, 0x12C) "emptyb" / "cemptyb" and (0x78, 0x12C)
   "emptyb" / "cemptyb" -- the scroll pair at the buy/sell pair's x, the reset and confirm buttons
   drawn from the same blank pad pair the buy/sell screen's fourth control uses.
10. 0x99698, 0x996B8: the confirm control goes to `x = 120 - (uWidth >> 1)` and `y = 28 * nBaseCount
    + 10`, the base count at 0x159D80 read with LDRSB. The buy/sell screen hard-codes 297 here; the
    transfer screen moves the button down as the player owns more bases, which is the only
    transfer-screen layout that depends on the count.

Transfer_ResetSite, in the original's order:

1. 0x9691C: `byte_1870F8 = 1` -- the flag is raised before the gate, so a blocked pass leaves it
   cleared rather than untouched.
2. 0x96920..0x96974: the gate. Blocked when `Base_CountItemQtyAcrossBases(abFacilityGrid,
   nSiteCount, byte_1870F4, 0)` is non-zero -- 0x9692C is the fourth of the five call sites that pass
   the site COUNT as the site index, because that is where the transfer flow keeps the record it is
   working on, and the base index is the signed byte the psi screen left in byte_1870F4 -- or
   `unk_1870D0` or `unk_1870F0` is non-zero (both LDRSB, so a negative byte counts), or
   `Smap_SumSiteScores()` is positive.
3. 0x96980..0x96990: when blocked, `byte_1870F8 = 0`, `byte_1870C8 = 1` (a dword store into a byte
   global, the same shape the buy screen's shortfall paths use) and `dword_1870B4[0] = 5` -- the
   fifth reason code, after the buy screen's 0 (living quarters), 1 (funds), 2 (hangar) and 3
   (stores). 4 -- alien containment -- is written by TransferScreen_ValidateCapacity at 0x96618, and 5 by Transfer_ResetSite here.
4. 0x96994..0x96A04: the pending quantities fold back into the count column. For every row of panel
   A: panel D aux += panel E aux, then panel E aux = 0. The loop is guarded by `nLineCount > 0` at
   0x969B0, and it runs whether or not the gate blocked.
5. 0x96A04..0x96AA0: the working site is cleared -- `bUnk518`, `uRecoveredSoldiers`,
   `bRecoveredCraftCount`, `bRecoveredSoldierAssign` and `bHasLoot`, then `auItemQty` over
   `Map_nObjectCount` entries and not the full 256. `bFlag0` and `bFlag1` are left alone. That is
   what tells this apart from BuySellScreen_BuildControls' teardown at 0x76C08: that one clears the
   two flags and all 256 quantities but never `bHasLoot`, and this one clears `bHasLoot` and never
   the two flags.

Deviation: the original allocates the pad and the four controls with `operator new` and dereferences
every one of them with no test; the port allocates only the pad and takes caller-owned controls,
storing them into ppRoot[1..4] so the rest of the screen finds them where the original put them.
`Transfer_ResetSite` indexes `aSmapSites[nSiteCount]` blindly; the port bounds it against
PUFO_SMAP_SITE_COUNT and returns NULL there.

IDB: function comments at 0x9955c, 0x9690c and 0x96ad8 (the merge note); `stru_AC3B8` ->
`g_aTransferCtlDesc`; comments at 0x1870D8 (the root array), 0x1870B4 (the reason codes, now
including 5), 0x1870F8 (the shared pending flag) and 0x1870D0 / 0x1870F0 -- the two gate bytes whose
only references anywhere in the binary are the two LDRSB reads here, with no direct write at all.


## TransferScreen_BuildPanels @ 0x96b30 and Transfer_MoveOneItem @ 0x9602c -- the transfer row list

Evidence: `TransferScreen_BuildPanels @ 0x96b30` (3956 B), called from Ui_InitTransfer @ 0x9955c
(0x995A4) and again from Geoscape_CloseUI (0x8b94c, not ported) at 0x8BE40, and `Transfer_MoveOneItem
@ 0x9602c` (604 B), the handler it installs into the decrement column. This is the transfer mode's
counterpart of BuySellScreen_BuildPanels @ 0x76de0: the same five panel globals, the same
g_aBuySellRowOrder sort, the same row model -- with a wider slot filter, a shifted geometry, no price
column and no alien row-marker offset. Test: `TestTransferBuildAndMove` in tests/test_baseui.c, 47
checks.

The built flag is `bTransferPanelsBuilt @ 0x1870EC` (IDB, was `unk_1870EC`), the third sibling after
bBuySellControlsBuilt @ 0x161A24 and bBuySellPanelsBuilt @ 0x161A28.

Teardown branch (0x97994..0x97A88): the flag is cleared, `byte_1870F8` is cleared, the pad at 0x1870D8
is `PadImage_FreeData`'d and deleted, the four controls at 0x1870DC..0x1870E8 are
`UiControl_FreePads`'d and deleted, and panels A, B, E, C, D are freed in that order. There is no
funds rollback here -- the transfer screen spends no money, which is the third thing that separates
the three teardowns from each other.

Build branch (0x96B5C..0x97988):

1. 0x96B60..0x96BDC: the flag is set and the working site `aSmapSites[nSiteCount]` is cleared exactly
   the way Transfer_ResetSite clears it -- `bUnk518`, `uRecoveredSoldiers`, `bRecoveredCraftCount`,
   `bRecoveredSoldierAssign`, `bHasLoot`, then `auItemQty` over `Map_nObjectCount` entries.
2. 0x96BF0..0x96C3C: count the soldiers at this base that no craft carries (`nKind` == base and
   `bUnk183` == 0xFF).
3. 0x96C44..0x96CB4: count the craft homed here with nobody aboard -- `bHomeBase` == base and
   `Craft_CountSoldiersAboard(craft index)` == 0.
4. 0x96CC4..0x96DC0: the slot filter is 1..69, 77..111, 114, 122..124 and 143..148 with a non-zero
   `auPerBaseVal[nSelectedBaseIdx]`, plus 112 and 113 when the base has engineers or scientists.
   Against the sell mode this is its stocked and alien filters merged -- the same slots except 142,
   which the sell mode counts and this screen does not. There is no separate alien pass.
5. 0x96DC4..0x9732C: the five panels, all `nStyle` 13 and `nFont` 16. A x 10 / viewW 125 with
   Transfer_CacheSelIndex @ 0x96ab0 as its click; E x 232 / 190 / 45, no click; B x 172 / 168 / 15
   with Transfer_MoveOneItem; C x 188 / 184 / 15 with TransferScreen_ValidateCapacity (0x96288, not
   ported); D x 166 / 130 / 50, no click. Against the sell mode the B, C and D columns are eight
   pixels further right and A and E are identical.
6. 0x97330..0x974DC: the soldier rows -- name through the same 17-character cut, panel A aux 1000,
   panel D aux 1, panel E aux 0, the "<" and ">" literals, panel C aux the roster id
   (Soldier.nUnk440) and panel B aux the apSoldiers index.
7. 0x974E4..0x976B4: the craft rows -- TEXTS[415 + bCraftType] + "-" + itoa(bType), panel A aux
   Transfer_RoleToIconId(bCraftType), panel D aux 1, panel E aux 0, panel C aux the craft id. Panel B
   aux is left at zero here, where the soldier rows set it.
8. 0x976C0..0x97858: the object rows. Panel A aux is the plain slot id for every row -- this screen
   has no +2000 alien offset, so an alien row sorts by its own slot number -- panel E aux is 0 because
   the transfer screen has no price column, and panel D aux is `auPerBaseVal[nSelectedBaseIdx]`, or
   the engineer / scientist count for slots 112 and 113.
9. 0x97880..0x97988: the same selection sort over g_aBuySellRowOrder, `nObjectCount - 1` passes,
   swapping panels A, E, B, C and D together.

Transfer_MoveOneItem (0x9602c) is the decrement column's handler. It reads the selected row's slot id
out of panel A at the row index panel B cached (`pBuySellPanelB->nUnk3032`, 0x9606C), then repeats
`nRadarPulseTimer / 10 + 1` times, re-reading the cached row index `unk_186BE8` on every iteration
(0x960B0):

- a craft row (panel A aux 117..121, tested at 0x960E4 as `> 116 && < 122`) with a pending unit
  clears panel E aux, raises panel D aux and decrements the site's `bRecoveredCraftCount` (+0x205),
  then stops -- one craft per click whatever the pending count was.
- any other row with a pending unit raises panel D aux, lowers panel E aux and decrements the site's
  `auItemQty[slot]`.
- the soldier row (slot id 1000) is handled once outside the loop at 0x961C0: panel E aux goes
  straight to zero, panel D aux rises by one and `bRecoveredSoldierAssign` (+0x204) is decremented.

All three site decrements read with `LDRSB` and store with `STRB` / `STRH`, so a count already at zero
wraps to 255 rather than staying put. The return value is whatever R0 happens to hold at the exit --
517 (the 0x205 offset the craft branch just used) after a craft decrement, the row aux when nothing
was pending, the site address after a soldier decrement, and the timer quotient at the loop bound --
and it is a click handler, so no caller reads it.

ORIGINAL BUG, recorded in docs/parity/bugs.tsv: the craft count pass at 0x96C94 tests only
`bHomeBase` and `Craft_CountSoldiersAboard`, while the craft fill pass at 0x97550 adds a third test,
`Ship.bAtBase` (0x06) non-zero. A craft homed at this base, carrying nobody and currently away is
counted into the row total but never written, so the list shows one empty row.
BuySellScreen_BuildPanels' own count pass at 0x76FB4 does test `bAtBase`, so the two screens treat the
same craft differently. The test asserts the mismatch rather than hiding it: eight rows counted, seven
names written.

Transfer_MoveOneItem is the fifth handler on the timer-bound repeat loop already recorded for
BuySellScreen_FillSellPanel, BuySellScreen_FillBuyPanel, BuySellScreen_ValidateCapacity and
BuySellScreen_UpdatePrices: one click of the transfer screen's "<" moves between 1 and 26 pending
units depending on a countdown the transfer screen neither owns nor initialises.

Deviation: the original allocates the five panels with `operator new` and deletes the pad and the four
controls in its teardown; the port frees only the pad and takes caller-owned controls. The original
dereferences every allocation with no test, and indexes panel A with whatever `nUnk3032` holds; the
port bounds the row index.

IDB: function comments at 0x96b30 and 0x9602c; `unk_1870EC` -> `bTransferPanelsBuilt` (typed
`unsigned __int8`); comments at 0x96C94 and 0x97550 naming the count / fill mismatch on both sides of
it, and at 0x1870EC.

TransferScreen_ValidateCapacity (0x96288, 1372 B) is the transfer screen's increment column, the mirror
of BuySellScreen_ValidateCapacity: where that one moves units into the base and spends money, this one
moves units out of the base onto the working site and spends none. The selected row's slot id comes
from panel A at the row index panel C cached (`pBuySellPanelC->nUnk3032`, 0x962D0) -- panel C, where
the buy screen used panel B.

Two shapes, decided by that slot id alone:

1. Slot 1000, 112 or 113 (0x96358): the while condition fails on the first test, so the repeat loop is
   never entered and the pass runs exactly once. The gate is Base_CalcLivingQuartersCap (0x96658)
   against Base_CalcOccupiedSpace (0x96668) + Base_CountOutboundTransferSpace with the scan on (0x96688)
   + 1; on shortfall byte_1870C8 is written unconditionally (0x96694) with reason 0. A soldier row then
   sets panel E aux to 1 -- assigned, not incremented (0x966E8) -- lowers panel D aux and raises the
   site's bRecoveredSoldierAssign (0x9672C). A crew row (112 / 113) instead raises the site's
   auItemQty[slot] by one, but only while auPerBaseVal[nSelectedBaseIdx] is strictly above what the
   site already holds (0x9678C), and lowers panel D aux and raises panel E aux.
2. Any other slot: the repeat loop, nRadarPulseTimer / 10 + 1 passes -- the same timer-bound bound
   recorded for the four buy-screen handlers and Transfer_MoveOneItem. A craft row (panel A aux
   117..121, tested at 0x96374) gates Base_CountInboundTransfers (0x96558) + Base_CountShipsAtBase
   (0x9653C) + 1 against Base_CalcHangarCap (0x96570) -- reason 2, written only when the flag is
   currently clear (0x96584) -- then moves at most one craft per click: panel E aux assigned 1
   (0x965C0), panel D aux lowered, site bRecoveredCraftCount raised (0x96604). Any other row first
   checks that the base actually has the item, auPerBaseVal[nSelectedBaseIdx] - site.auItemQty[slot]
   > 0 (0x963C0). For an alien slot (77..104 or 114, 0x963D8) it then gates
   Base_CalcAlienContainmentCap (0x963EC) against Base_CountAmmoItems + 1 (0x96400) -- reason 4,
   written unconditionally (0x96614) -- and finally gates Base_CountItemQtyAcrossBases (0x96434) +
   bCargoSize + Base_CalcStorageUsed (0x96418) against Base_CalcStoresCapacity (0x96458) (reason 3,
   written only when the flag is clear, 0x9662C). On the way through it raises the site's
   auItemQty[slot] (0x96494), lowers panel D aux (0x964A8) and raises panel E aux (0x964C8).

The two base indices are not the same one, and the direction explains it: every capacity gate measures
the base the transfer was opened for (byte_1870F4, read signed), while the "does the base actually
have this" test reads auPerBaseVal at nSelectedBaseIdx (0x159D81). Items leave the geoscape-selected
base and are checked against the destination's facilities; the buy screen uses nSelectedBaseIdx on
both sides because there the destination is the selected base.

Deviation: the original returns &dword_1870B4 on a shortfall and 517 (the 0x205 offset the craft
branch just used) after a craft move; the port returns the reason code, matching
BuySellScreen_ValidateCapacity. The port bounds the cached row index and the site slot; the original
trusts both.

IDB: function comment at 0x96288; the local enum PufoShortfallReason declared for the reason codes
dword_1870B4[0] holds (-1 none, 0 living quarters, 1 funds, 2 hangar, 3 stores, 4 alien containment,
5 transfer blocked); comment at 0x1870B4 listing which pass writes which code.

TransferScreen_CommitTransfer (0x97aa4, 1280 B) is the confirm control Ui_InitTransfer wires at
0x99608. It writes the queued rows into the working site, unhomes the queued soldiers and craft, and
-- only when the transfer actually carries something -- closes the working record and opens a fresh
one.

The row pass (0x97AF8..0x97D24) walks panel A's rows and acts on two kinds, both gated on panel E aux
being exactly 1, the value the capacity gate assigns. A soldier row (aux 1000) writes panel C aux --
the roster id the row carries -- into aSmapSites[nSmapSiteCount].anSoldierIds[nSoldiersQueued]
(0x97B58; the original stores a dead "+1" there first and then overwrites it with the id), then gives
every soldier whose nUnk440 equals that roster id a Soldier.nKind (0x0B6) of 255 (0x97BB0, STRB #0xFF):
the soldier leaves the base roster, the mirror of the craft branch. A craft row (aux 117..121) writes
the craft id into anCraftIds[nCraftsQueued] (0x97C48, same dead "+1" first), then gives every craft
whose Ship.bId equals that id a cleared hangar cell, abFacilityGrid[444 + 144*bHomeBase + 6*bHangarY +
bHangarX] (0x97CCC, built as 6 * (24*bHomeBase + bHangarY + 12) + bHangarX over abFacilityGrid + 0x174),
and a Ship.bHomeBase of 255 (0x97CD4). A row whose aux is 1000 but whose panel E aux is not 1 skips
the craft test as well: 0x97B10 branches to the next row, not to the craft block.

The commit gate (0x97D28..0x97D90) runs the commit only when at least one of: Base_CountItemQtyAcrossBases
with the scan off, the site index at the count and the base at byte_1870F4 non-zero; a soldier or a
craft queued; the working site's auItemQty[112] + auItemQty[113] non-zero; Smap_SumSiteScores above
zero. With nothing pending nothing at all happens. The commit body then sets bFlag0 = 24 on the record
just filled (0x97DA4 -- a completed transfer, where the shop's "open" flag is 8) and bFlag1 to the
destination base's type byte (0x97DBC), zeroes panel E aux over every row (0x97DDC), subtracts every
pending quantity from the SELECTED base through BaseRec_SubItemQty (0x97E5C), raises nSmapSiteCount
(0x97E8C), reports every object whose auPerBaseVal[nSelectedBaseIdx] exceeds 60000 (0xEA60) through
Transfer_WriteErrorDump (0x996e4) with the base index and the object index (0x97EE4), and finally clears the
fresh working record at the new count: bUnk518, uRecoveredSoldiers, bRecoveredCraftCount,
bRecoveredSoldierAssign, bHasLoot and auItemQty[0..nObjectCount-1].

Deviation: when the gate skips, the original returns the Smap_SumSiteScores count reinterpreted as a
pointer; the port returns NULL. The port bounds the two id arrays (100 and 10 entries) and the
bHomeBase used to index the grid; the original trusts all three, so a craft already sitting at
bHomeBase 255 would write 24*255 + ... bytes past the facility grid.

IDB: function comments at 0x97aa4 and 0x186410; `nSmapParam4` -> `nSmapSiteCount`, the number of
SmapSite records, with aSmapSites[nSmapSiteCount] the record the screens work on.

TransferScreen_Draw (0x97fa4, 2988 B) is the transfer screen's frame, reached from Geoscape_Draw
(0x8c180, not ported).

Frame (0x97FB8..0x98150). The side rails run 24 steps: at x 5 and x 234 a 1x7 block at y 2 + 7k with
index 5*(k + 20) (100..215) and a 1x7 at y 152 + 7k with the ARM's RSB #0xE6 index 230 - 5k
(230..115). The climbing blocks are 7 tall on a 7 step, so they run 1..168 with no gaps and, being
drawn before the falling blocks of the same step but in a later iteration, they own 152..168: the
falling half only becomes visible from 169. The top and bottom rails run 24 steps of 5, a 5x1 at
x 1 + 5k and at x 119 + 5k on y 5 and y 315, climbing 5*(k + 20) on the left half and falling
250 - 5k (RSB #0xFA) on the right; the two halves overlap at 119..120 and the later climbing block
wins. Four 1-pixel separators stand at x 130, 168, 183 and 198 from y 69, height
16 * min(pPanelB->nLineCount, nStyle) + 2, index 0xB4.

Panels (0x98224..0x9828C), only while bTransferPanelsBuilt: panel B and panel C get bUnk7080 = 1 (the
original routes panel C's value through unk_161F0C, setting it to 1 and back to 0 on the way), then
the five panels draw through UiPanelMsg_Draw in the order A, E, C, B, D. With the overlay closed, a
base preview armed and the map cursor inside the row band, the row under the cursor is named at
(10, 282): TEXTS[159] "Soldier" for slot 1000, otherwise apObjects[slot] read as its own szName.

Labels (0x98388..0x98510): TEXTS[1264] "Destination Base" centred at (60, 300), TEXTS[1265]
"Transfer" at (180, 300), TEXTS[1266] "From: " with the source base's name at (120, 20). The base
names come from the eight 64-byte records at 0x186C40, name field at +26 (0x186C24[64*i + 54]).

Shortfall (0x98538..0x986B4): when the shortfall dword byte_1870C8 is 1, reasons 0..5 each animate a
200x70 panel at (20, 130) through Ui_AnimExpandBlit_IfEnabled and draw TEXTS[405], [406], [407],
[408], [1443] and [1444] -- "Not enouth living quarters", "Not enouth money", "Not enough free hangar
space", "Not enough storage space", "Not enough containment space" and "Transfer is not complete"
(the first two carry the shipped typo) -- centred at (120, 158), and the draw returns there. With the
overlay closed, TEXTS[1269] "TO: " with the destination base's name at (120, 32), and the draw
returns.

Destination overlay (0x986F8..0x98A98): a full-screen 240x320 blend, a 32-wide strip at x 208 and a
210-wide panel both 28 * nBaseCount + 4 tall, the source base's facility icons through
Base_DrawFacilityIconStrip (0x6250c, not ported), then one bevelled row per base -- three nested
rects at 0x46/0xA0/0xB3, 0x00/0x6E/0x77 and 0x04/0x46/0x4B -- with the base's name centred at
(110, 28i + 7). The confirm control blits under the list, TEXTS[1267] "Stop Transfer" centred at
(120, 28 * nBaseCount + 10), and the tap resolves through Base_HitTestFacilityStrip: the result lands
in byte_1870F4, the working site's bFlag1 takes that base's type byte (0x989C4), and a tap on any
base other than the source closes the overlay (0x989E0). Both cursor coordinates are zeroed,
TEXTS[1268] "Select destination base" draws at (120, 282) with its red channel at 250 - 40 *
nMapPhase, and the working site's bFlag1 is written once more from byte_1870F4 (0x98A98).

Deviation: the original builds every label in a 140-byte stack buffer with unbounded copy loops and
two strcats; the port bounds each build to the same 140 bytes. It returns register residue; the port
returns the site count on the overlay path and 0 on the two early returns. The port null-guards the
panels, controls, object array and site array.

Visual check: TestTransferDraw now writes the rendered 240x320 logical surface out as a BMP for each
of the three states (closed, shortfall, overlay) and the frames were inspected. The rails, the four
separators at 130/168/183/198 from y 69, the navy full-screen blend and the two bevelled base rows
all land where the decode says they should. Text does not appear in the harness dumps: Text_DrawString
draws through palette slot 2 (g_nGlyphPalSlotBase = 2) and the glyph sprites, and the unit-test
harness does not run the geoscape bring-up that loads them; the app's own `--dump-frame` path covers
the live screens. Recorded as a separate follow-up, not a port defect.

IDB: function comment at 0x97fa4; aTextRec1264..aTextRec1269 and aTextRec159 named for the labels
this draw reads.

Base_DrawFacilityIconStrip (0x6250c, 792 B) draws the base-tab strip down the right edge of the
geoscape, the thing Base_HitTestFacilityStrip (0x671B0) hit-tests.

Arguments (0x62520..0x62544). Every caller passes &abFacilityGrid in R0 -- BaseInfoScreen_Draw
(0x57488), PsiScreen_Draw (0x727d8, with the mark argument -1), Geoscape_Draw twice
(0x922c0, 0x92d40) and TransferScreen_Draw (0x98744, with nSelectedBaseIdx) -- and the function reads
its two count bytes back out of that pointer at +0x618 and +0x619, which are nBaseCount @ 0x159D80
and nSelectedBaseIdx @ 0x159D81. When they differ it first blits a 26x26 erase rect at
(211, 28 * nSelectedBaseIdx + 3) with 0xFF / 0xFF / 0x64 (0x6257C); with no base selected the byte
reads as -1 and the rect lands at y -25, off screen.

Cells (0x625C4..0x62818). One 6x6 block per base, 28 pixels apart, at x = 212 + 4 * column and
y = 28 * base + 4 + 4 * row. Each cell reads two bytes of the grid:
abFacilityGrid[36 * base + 6 * row + column], the buildDays timer, and
abFacilityGrid[372 + 144 * base + 6 * row + column], fac[base].tileA. A cell with no tile is grey --
4x4 at 0x14 and 2x2 at 0x28, all three channels equal (0x626B8) -- and because the grey pair is drawn
after the status pair it wins, so an empty cell never shows a status colour. A cell with a tile gets
a status marker: with a buildDays timer it puts 0x96 in the red field and 0xFF in the 2x2 (0x6260C),
without one it puts 0x96 in the green field (0x62660).

The mark (0x62720..0x62798). The second argument selects a base row and draws a chevron: 23 two-pixel
blocks down-right from (212, 28 * base + 4) and 23 up-right from (212, 28 * base + 26), meeting at
x 234. It sits inside the per-cell loop, so the original repaints the same 46 blocks 36 times for the
marked base -- idempotent, and reproduced. -1 matches no row. The loop runs on the register holding
the row's y and decrements it 23 times; the loop end reloads it from the stack slot, so the
corruption is discarded and the port computes both diagonals from the row's y.

Deviation: the original takes the grid pointer as an argument and indexes 1524 bytes of it plus two
bytes past its end for the counts; the port takes the counts as their own pointers, as the hit test
already does.

Found while porting it: TransferScreen_Draw's destination-overlay close branch is unreachable
(0x989D0, bugs.tsv). The draw returns at 0x98224 unless bTransferPanelsBuilt is set, and
Base_HitTestFacilityStrip disables the strip on that same byte, forcing a miss that returns
nSelectedBaseIdx -- the very value 0x989D0 compares against. The overlay is never closed by this
code, and the store at 0x98994 pins the transfer destination byte_1870F4 to the source base on every
overlay frame. Reproduced, not fixed.

IDB: comments at 0x6250c and 0x989D0.

Base_DistributeRecoveredItems (0x68f2c, 1216 B) re-arms every craft sitting at the base being
processed. Callers are Smap_GrantSiteRecovery (0x98b50) and the landing chain. It opens with
Base_StripAllCraftCargo (0x68F3C) and then walks bases outer (nBaseCount, read signed from
abFacilityGrid + 0x618), craft inner (Map_nCraftCount, read unsigned).

Gate (0x68FA0): Ship.bAtBase != 0 and Ship.bHomeBase == nBase.

Mounts (0x68FDC jump table). Ship.bWeaponL @ 0x22 and Ship.bWeaponR @ 0x23 hold weapon object slots,
and five of them have a resupply rule, each keyed to its ammo record and to the craft's own quantity
byte at 0x3d..0x41:

    25 Craft cannon         -> apObjects[52]  "Craft cannon rounds[x50]"  10 per mount (0x69174)
    26 Stingray launcher    -> apObjects[53]  "Stingray rocket"            3 per mount (0x69120)
    27 Avalanche launcher   -> apObjects[54]  "Avalanche rocket"           2 per mount (0x690D8)
    29 Plasma cannon        -> apObjects[105] "Elerium-115"               10 per mount (0x69074)
    30 Fusion ball launcher -> apObjects[55]  "Fusion ball"                2 per mount (0x6900C)

Weapon 28 (Laser cannon) and 24 (Psi-arm) fall through the switch and are left alone; the laser is
the weapon that never runs out. The names come from main.dat TEXTS.DAT at 0x2a9ff6, record 1446 + slot.

Load factor (0x68FC4): 2 when both mounts hold the same weapon, 1 otherwise, so a twin-mount craft
re-arms with double the standard load.

Each mount, both passes (0x6900C..0x691B4 left, 0x691F4..0x69388 right): the craft's quantity is
added to the base, its byte is cleared, and the craft is then re-supplied with perMount * load. If
apObjects[slot]->auPerBaseVal[nBase] is below that, the craft takes what the base had and the base's
count is zeroed (0x69150) with no BaseRec_SubItemQty; otherwise the craft takes the full load and the
base loses it. The net across a mount is exactly what the craft gained.

Elerium is the odd one out: its craft byte is in tenths. The base receives bAmmo29 / 10 and the byte
written back is 10 * what the craft ends up carrying -- 100 * load when the base can supply it,
10 * the base's count when it cannot (0x69094..0x690BC, the ANDLT/MULLT pair).

Both mounts run even when the first matched no weapon, and when both hold the same weapon the second
pass reads the byte the first pass has just refilled and hands that back to the base as well. That is
what the code does and the port reproduces it.

Deviation: the per-base count is indexed by the loop counter here (auPerBaseVal at record + 42 + 2 *
nBase, R6 = 0x2A stepped by 2), while BaseRec_AddItemQty and BaseRec_SubItemQty index it by
BaseRec.bSlot. The port keeps both, as the original has them. The port also bounds the base loop at
the eight real base records; the original bounds nothing.

IDB: comment at 0x68f2c.

## Smap_GrantSiteRecovery (0x98b50, 2572 B) -- mission recovery at the hourly tick

Ported as `Smap_GrantSiteRecovery` in `src/pufo/game/pufo_baseui.c` with the env
`SmapGrantRecoveryEnv` in `pufo_baseui.h`, tested by `TestGrantSiteRecovery` in `tests/test_baseui.c`.

One caller: `Geoscape_TickHourly @ 0x5d1bc`. Three phases over the site array at 0x162030.

Phase 1 (0x98B68..0x98BAC): every site with a positive bFlag0 is decremented -- the countdown phase 2
tests for zero.

Phase 2 (0x98BB0..0x994C4), per site with bFlag0 == 0:

  Base lookup (0x98BE0..0x98C24): the first apBases[i] whose BaseRec.bUnk4 @ +4 equals site->bFlag1,
  scanned while i < nBaseCount (read signed from abFacilityGrid + 0x618). A miss leaves i AT nBaseCount.

  Stores gate (0x98C30..0x98CF0): with bHasLoot set, Base_CalcStorageUsed + Base_CountItemQtyAcrossBases
  (scan ON, this site as the gate site) is tested against Base_CalcStoresCapacity; over budget writes 1
  into byte_1870C8 as a dword and posts message 42. With bHasLoot clear the shortfall is written
  unconditionally and message 41 is posted. The item loop runs either way.

  Item loop (0x98D00..0x99058) over every object slot. The compare chain at 0x98D0C..0x98D34 picks out
  the alien slots -- 77..104, 114 and 144..148 -- and gives them one extra test: each alien slot with a
  non-zero quantity raises a running counter (var_4, set up at 0x98B70) held against
  Base_CalcAlienContainmentCap for the base. The counter counts SLOTS, not quantity, and it is not reset
  per site: it accumulates over the whole call, so a later site sees what earlier sites already put into
  containment. Over cap: shortfall dword set, message 36, and the quantity MOVES to that alien's corpse
  slot with the source slot zeroed -- the two-switch remap at 0x98DBC..0x98F98:

      77..81 -> 59 Sectoid corpse     82..86 -> 60 Snakeman corpse
      87..91 -> 61 Floater corpse     92..94 -> 62 Muton corpse
      95..98 -> 63 Ethereal corpse    99 -> 64 Cyberdisk remains
      100 -> 65 Chryssalid corpse     101 -> 66 Reaper corpse
      102 -> 68 Silacoid corpse       103 -> 67 Celatid corpse
      104 -> 69 Sectopod remains      114 -> 63 Ethereal corpse
      144 -> 60 (the default)   145 -> 62   146 -> 61   147 -> 63   148 -> 59 (the > 145 fall-through)

  Everything else, and every alien that still fits, takes the normal path (0x98FC8..0x99040): the
  quantity is added to the base except for slots 117..121 (the new-craft counts, delivered as craft
  below), Transfer_WriteErrorDump (0x996e4, not ported) fires when the base's column for the slot
  passes 60000 (0xEA60 at 0x9902C), and Base_DistributeRecoveredItems runs after EVERY slot -- empty
  slot or not.

  Recovery (0x9905C..0x99468): uRecoveredSoldiers calls of Soldier_CreateAndClassify with the base
  index as nKind; per recovered craft the Ship whose bId matches anCraftIds[i]; for i in 0..4,
  auItemQty[117 + i] new craft of type {0, 3, 4, 1, 2} through Ship_SpawnAtSlot, homed at the same
  base (the five slots carry the shipped craft names and the map lands each name on its own
  CraftType); and per anSoldierIds[i] with i < bRecoveredSoldierAssign the roster soldier whose
  nUnk440 matches gets nKind = the base index.

  Hangar scan (0x99130..0x99314): facType cell == 11, layer2 tested at fac + 79 + row + col,
  buildDays cell == 0, rows and columns 0..4 of a 6 by 6 grid. On a match bHangarX = col + 1 (+0x12),
  bHangarY = row + 1 (+0x13), and the glyph 33 + bCraftType is stamped at fac + 79 + 6*row + col. Two
  differences from Ship_SpawnAtSlot's own scan (0x2cb14): the layer2 test here follows the row
  (79 + row + col, not 79 + col), and a craft type outside 0..4 CONTINUES the scan (0x991F8) instead
  of returning unstamped.

  Mark and refresh (0x99484..0x994A0): bFlag0 = -1, then Base_UpdateAlienContainmentNeeded,
  Base_UpdateLivingQuartersNeeded and Base_UpdateStoresNeeded with the budgets the original computes
  inside them: Base_CountAmmoItems; Base_CalcOccupiedSpace + Base_CountOutboundTransferSpace with
  a2 = -1; Base_CalcStorageUsed + Base_CountItemQtyAcrossBases with a2 = -1.

Phase 3 (0x994C8..0x99558): sites marked -1 are dropped by shifting the tail down one slot and
nSmapSiteCount is stored back. The shift copies 371*(count - index) WORDS -- one record MORE than the
records still inside the count -- and that extra record (the unused slot after the last live site,
which carries bFlag0 == 0) is what ends the inner loop; the index is re-tested after every shift, so a
run of delivered sites collapses in one pass. With a full 200-site array the extra read lands on
nSmapSiteCount itself at 0x186410, whose low byte 200 is not -1, so the loop still terminates.

Deviations: the original bounds nothing in this function. A base-lookup miss continues through
apBases[nBaseCount] -- the port marks the site delivered and skips the delivery. A craft- or
soldier-lookup miss writes through apCraft[Map_nCraftCount] / apSoldiers[nSoldierCount] -- the port
skips that craft or soldier. The compaction's inner loop is guarded only by the copy, not by the
count -- the port bounds it too. The original returns the viewport-mode global at 0x1863E8 (register
residue, no caller reads it); the port returns void.

IDB: comment at 0x98b50.

## Base_TickFacilityConstruction (0x62824, 396 B) -- the daily build-timer countdown

Ported as `Base_TickFacilityConstruction` in `src/pufo/game/pufo_geoscape.c` with the env
`BaseTickConstructionEnv`, tested by `TestTickFacilityConstruction` in `tests/test_geoscape.c`.

Called from `Geoscape_TickBases @ 0x5d3bc` with abFacilityGrid as its only argument. For every base
(nBaseCount, read signed from abFacilityGrid + 0x618) it walks that base's buildDays 6x6 block and
decrements every non-zero byte (0x62878..0x6288C). When a byte reaches zero the build finished:

  - 0x628B4: if the finished cell's facType kind is 11 (hangar), three MORE buildDays bytes are
    cleared -- grid + 36*base + 6*row + col + 6 (0x628C4, directly below), + 7 (0x628CC,
    below-right) and + 1 (0x628D4, to the right). These are the days block's own arithmetic on the
    running pointer R8 (j+7 and j+1), not a facility layer, and at row 5 they land in the NEXT
    base's days block. The grid is big enough for that and the port reproduces it.
  - 0x628E4..0x62920: the six needed-flag passes for the base, with the budgets the original
    computes inside them (ammo items; active research; occupied space plus outbound transfer space
    with the -1 gate; psi soldiers; storage used plus item quantity across bases with the -1 gate;
    active manufacturing).
  - 0x62934..0x62948: byte_1870C8 = 1 as a dword and message 38.

The message payload is `(38, base, row, col, 0)`. `UiPanel_AppendLineWithSound` takes SIX
arguments, so a3 is R2, a4 is R3, a5 is `[SP+0]` and a6 is `[SP+4]`: 0x62938 puts the base in R2,
0x6292C's `AND R3, R7, #0xFF` puts the ROW in R3, 0x62944 (`STRB R5`) stores the COLUMN at `[SP+0]`
and 0x62930 (`STRB R9`) stores R9 -- the zero the completion writes with -- at `[SP+4]`.

An earlier pass here claimed the payload was `(38, base, col, 0, 6*nBase)`, reading the caller's
`var_8` (the 6*nBase running offset) as a seventh argument. That was wrong: with a 0x10-byte frame
`var_10` and `var_C` are `[SP+0]` and `[SP+4]` -- the two stack arguments -- and `var_8` sits at
`[SP+8]`, past the callee's last parameter. Hex-Rays had it right. The port now passes
`(38, base, row, col, 0)` and the test asserts all four payload slots, including a base-1 case so
the values are four different numbers.

Deviation: the original returns the last day byte it read (register residue); the port returns void.

IDB: comments at 0x62824 and 0x62948 (the payload correction).

## ManufScreen_RefreshBaseTabs (0x70ebc, 500 B) -- the manufacturing refresh at the hourly tick

Ported as `ManufScreen_RefreshBaseTabs` in `src/pufo/game/pufo_baseui.c` with the env
`ManufRefreshEnv`, tested by `TestManufRefreshBaseTabs` in `tests/test_baseui.c`.

Called from `Geoscape_TickHourly @ 0x5d304` with the product-block anchor 0x159E98. Per base and
product slot it reads `auManufCol[200*base + product]` (the u16 table at 0x15E19C, reached as
anchor + 400*base + 2*product + 17156) and, when non-zero, adds it into
`auProductU32[200*base + product]` @ 0x159E9C.

  - Products 119..121 -- the three the hangar branch of `ManufScreen_ClearProductSlot` covers -- are
    added with no capacity test at all (0x70F54..0x70F6C).
  - Every other product is tested first (0x70F70..0x70FCC): `Base_CalcStorageUsed` for the base,
    plus `Base_CountItemQtyAcrossBases` with the scan ON and the -1 gate, plus that product's own
    `ObjectRecFull.bCargoSize`, against `Base_CalcStoresCapacity`. Strictly under capacity the
    quantity is added (0x70FE8); at or over it, it is not. Both capacity terms are recomputed for
    EVERY product -- that is what the original does, and the port keeps it.
  - At or over capacity (0x71060): the shortfall dword is raised, message 32 is posted, and the
    product slot is cleared through `ManufScreen_ClearProductSlot`. Then the function RETURNS --
    0x710A4 branches past the commit call -- so one over-budget product ends the whole refresh,
    including the remaining bases.

The message payload is `(32, base, base, product, 0)`. Six arguments again: a3 = R2 and a4 = R3
both hold the base (0x7107C and 0x71084), a5 = `[SP+0]` holds the product (`STRB R5` at 0x7108C)
and a6 = `[SP+4]` holds 0 (`STRB R3` at 0x71078). An earlier pass here claimed the ARM said
`(32, base, product, 0, base)`; that came from the same frame-offset misreading as message 38 and
was wrong -- with a 0x18-byte frame the two stack arguments are `var_18` and `var_14`, and `var_10`
(the base index) is a local at `[SP+8]`. This is also the shape messages 33, 34 and 35 use in
`ManufScreen_CommitProduction`. The test now uses base 1 and product 7 so the four slots hold four
different values.

When the loop finishes with no over-budget product the original calls `ManufScreen_CommitProduction`
(0x6f34c, not ported) on the way out, and makes the same call when the base count is zero. The port
leaves that call out and records it.

Deviation: the original walks the quantity and accumulator tables with running pointers and bounds
nothing; the port indexes them as 200*nBase + product.

IDB: comment at 0x70ebc.

## Ship_ProductSlotToType (0x65a6c, 88 B) -- the craft product slot -> CraftType map

Renamed in the IDB from `BaseFac_RaceCharToIndex`: nothing in the 88 bytes is a race, and the five
values it compares are the craft-count object slots. Ported as `Ship_ProductSlotToType` in
`src/pufo/game/pufo_ship.c`, tested by `TestProductSlotToType` in `tests/test_ship.c`.

`AND R0, R1, #0xFF` at 0x65A6C, then a five-way compare chain on the masked byte:

    117 -> 0 Skyranger   118 -> 3 Interceptor   119 -> 4 Firestorm
    120 -> 1 Lightning   121 -> 2 Avenger       anything else -> -1

Those five slots are the craft-count object records, and TEXTS.DAT records 1563..1567 (main.dat file
offset 0x2a9ff6, record 1446 + slot) name them SKYRANGER, INTERCEPTOR, FIRE STORM, LIGTHNING (sic)
and AVENGER. Each slot name lands on its own CraftType, so the map is a bijection over 0..4; the
test asserts exactly that. The mask is real behaviour, not decoration: a slot id wider than a byte
folds back onto the byte, so 0x175 answers Skyranger.

Only one code caller, `ManufScreen_CommitProduction @ 0x6F638`. It masks the product index itself
(AND R1, R7, #0xFF), leaves the grid pointer in R0 -- the callee never reads R0 -- and sign-extends
the answer with LSL#24/ASR#24 into `Ship_SpawnAtSlot`'s nCraftType byte, so an unmapped slot would
spawn a craft of type 0xFF. The caller's own guard at 0x6F5F4 (product <= 118 or product >= 122
takes the item branch at 0x6F694) keeps the call to 119..121, so the -1 answer is unreachable there.

`Smap_GrantSiteRecovery @ 0x98b50` carries the same map as an inline switch on the slot INDEX at
0x99360..0x993A8 (0->0, 1->3, 2->4, 3->1, 4->2). The port calls this one function from both places
instead of keeping a second copy, and the test asserts the two maps agree value by value.

Deviation: the original takes two arguments and never reads the first; the port takes one.

Also fixed while here: the `Ship_SpawnAtSlot` header comment named its 0x6F650 caller
`BaseFacScreen`, a function name the IDB no longer has; it is `ManufScreen_CommitProduction`.

IDB: renamed at 0x65a6c, with a comment there recording the map, the caller's byte extension and the
inline copy at 0x99360.
