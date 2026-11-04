# Tactical craft template selection

## Finding

The map builder selects the transport template with `Map_nMapLayers - 1`. The port used `Map_nMissionType - 1` instead. In the saved ground run, map layers are 1 and mission type is 2. The original therefore loads `plane.ma1`; the old port loaded `lightnin.ma1`.

That wrong file explains the empty floor at unit cell `(24,39,1)`. The layer-one template cell for local `(4,9)` is floor value 4 in `PLANE.MA1`, and 0 in `LIGHTNIN.MA1`. The same values hold at all eight first deploy offsets `(4/5,9..12,1)`. The port's cell indexing and template level walk match the raw code; no rotation, copy, or level offset change was needed.

## Raw ARM evidence

At `Map_BuildMissionTerrain @ 0x22e10`, bytes at `0x22e60..0x22e70` are:

```text
0x22e60 E3A03A15  MOV R3, #0x15000
0x22e64 E3833F45  ORR R3, R3, #0x114
0x22e68 E1A00004  MOV R0, R4
0x22e6c E7F03003  LDRB R3, [R0,R3]!
0x22e70 E58D0020  STR R0, [SP,#0x124+var_104]
```

The first two words form offset `0x15114`; the load reads `Map_nMapLayers` and leaves a pointer to that byte in `R0`. The pointer is saved at stack slot `var_104`.

That byte is not the count of allocated vertical cell planes. The allocation loop starts its layer register at 0 (`0x22ecc E3A04000`) and calls `MapCell_Init` with it (`0x22f04 E1A03004`, `0x22f0c EB00C68C`). It advances the cell table by `0x3840` bytes and stops at 4 (`0x22f14 E2866DE1`, `0x22f18 E3540004`, `0x22f1c BAFFFFEC`). Thus this map always has four backing levels even when `Map_nMapLayers` is 1.

The plane-name selection is:

```text
0x23030 E3A0B009  MOV R11, #9
0x231e4 E59D3020  LDR R3, [SP,#0x124+var_104]
0x231ec E59F0120  LDR R0, [PC,#0x120]       ; literal at 0x23314
0x231f0 E5D33000  LDRB R3, [R3]
0x231f4 E0010B93  MUL R1, R3, R11
0x231f8 E0813000  ADD R3, R1, R0
0x231fc E2431009  SUB R1, R3, #9
0x23204 EB00C666  BL  Map_LoadMa1
```

The literal at `0x23314` is `0x000A6668`. Bytes at that table address begin:

```text
70 6C 61 6E 65 00 00 00 00   "plane"
6C 69 67 68 74 6E 69 6E 00   "lightnin"
61 76 65 6E 67 65 72 00 00   "avenger"
```

So the selected base name is `aPlane + 9 * (Map_nMapLayers - 1)`. With map layers 1, it is `plane`. Mission type is read separately in the terrain-name path; it does not choose this table entry.

The template cell index is also raw ARM. `CDialogTemplate_GetCellByte @ 0x54c80` multiplies the level by `header0 * header1`, the row by `header1`, adds the column, scales by four, adds `nSlot`, then loads byte `+3`. In compact form:

```text
index = 4 * (header0 * header1 * level + header1 * row + column) + nSlot + 3
```

The instruction words at `0x54cb0..0x54cec` are `E20A30FF E20B20FF E0020293 E20730FF E0030392 E20020FF E20610FF E0010192 E20520FF E0833001 E0833002 E5DD201C E0823103 E5942000 E0833002 E5D30003`. They mask the header and coordinate bytes, form both products and sums, load `nSlot` from the stack, scale by four, add the blob base, then read byte `+3`. On the first slot pass, `0x54db8 E3A0A000` sets R10 to zero and `0x54dc0 E5CDA000` writes it as `nSlot`. For a value above 1, the template routine resolves the sprite and passes it as R1 to `MapCell_SetTileSprites`; the setter stores nonzero R1 at `[R0]` (`0x548d0 E3510000; 0x548d4 15801000`).

At `Map_ApplyTemplateToCells @ 0x54cf4`, `0x54d5c E3A03000` starts `nZ` at 0. After each cell-level pass, `0x54f90 E2835001` advances `nZ`, and `0x54f94 E2893DE1` advances the cell-table base by `0x3840` bytes, or 3600 four-byte pointers. The raw `MapCell_SetTileSprites @ 0x548d0` begins `E3510000 CMP R1,#0; 15801000 STRNE R1,[R0]`: its first sprite argument is the floor pointer at cell offset 0.

The deployment table bytes at `0xA66B8` start with:

```text
04 09 01 00  05 09 01 00  04 0A 01 00  05 0A 01 00
04 0B 01 00  05 0B 01 00  04 0C 01 00  05 0C 01 00
```

For the recorded craft tile `(2,3)`, the first point gives map cell `(24,39,1)`, which maps to plane-template local cell `(4,9,1)`.

## Archive evidence

The directory entry in the root `main.dat` is `PLANE.MA1` at offset `3185478`, size `2403`. Its header is `(20 rows, 10 columns, 3 levels)`. For local `(4,9,1)`, the raw byte index is:

```text
4 * (20 * 10 * 1 + 10 * 9 + 4) + 0 + 3 = 1179
```

That byte is 4. At the same coordinates, the `LIGHTNIN.MA1` entry at offset `3183075` has header `(10,20,3)` and byte index 1539; that byte is 0. Its first eight deploy cells all have floor byte 0, while `PLANE.MA1` has floor byte 4 at all eight.

The saved run records `metadataLayers=1`, `missionType=2`, and XCOM cells `(24/25,39..42,1)` in `build/e2e_normal_ground_play/run_20261026T203234Z_1209/normal_ground_play.log`.

## Change and regression check

`Terrain_PlaneName` now takes `nMapLayers`, and the normal terrain builder passes `*pEnv->pnMapLayers`. `TestPlaneTemplateUsesMapLayerSelector` runs the builder with map layers 1 and mission type 2, uses the real archives, and checks the floor sprite at all eight layer-one deploy cells.

At the worker handoff, no build/test or IDB edit had been made. The later parent
batch built the Debug App and `test_terrain` cleanly; the terrain regression
passed. The natural flow then moved, fired, completed an enemy turn, and reached
debrief/geoscape: [batch evidence](../../.workers/luna-runtime-corrected-port/RESULT.md).
The return helper/checker still failed, so this is not a full mission pass.

`nMapLayers` is a legacy field name. This byte selects a craft template and its
deployment row in these paths; it does not limit the backing grid, which retains
four layers. The regression deliberately uses different mission and selector
values so the wrong argument cannot pass by coincidence.
