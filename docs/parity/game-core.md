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
| `Unit_FormatDisplayName` @ `0x476d4` | 528 | Needs `pUnit->szDisplayName` (offset `+0x2CE`) and the `aszTextA`/`aszTextE` rank tables, which are inside the not-yet-modelled Soldier tail |
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

`SaveGame` @ 0x44988 and `LoadGame` @ 0x44bbc themselves � the port implements the
container and the soldier chunks, but the full slot flow also needs `bases`,
`product`, `research`, `ships`, `smap`, `strateg`, `time` and the four tactical
chunks, which belong to subsystems that are not ported yet. `SaveSlot_FormatList`
@ 0x45904 also remains.

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

**Fall damage.** `nFall = pCell->nZ - nZFound - 1` — the cell's own `nZ` field minus
the array index found by the walk, not two array indices. Then
`v52 = trunc_soft(nFall * auStat[1] * 0.74)` where 0.74 is the soft-FPU constant
`1053609165` (0x3F3D70A7 as a single-precision bit pattern). Clamped to 0 when
negative. If `uHp > v52`: `uHp -= v52` and `nUnk436 += v52`; otherwise `uHp` is set
to the accumulated `nUnk436`. **This needs a disassembly check at 0x4ef20**: read
straight from the pseudocode, `pCell->nZ` and `nZFound` look equal, which would make
`nFall` always -1 and the damage always zero.

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
