# Parity evidence — UI core

Task: recreate the shared UI layer (panels, fonts, text) that every screen draws
through.
Test: `tests/test_panel.c` (plus `tests/test_font.c` for the text layer).

## Files created and the functions they answer to

| Created | Original evidence |
|---|---|
| `ui/pufo_panel.{h,c}` | `UiPanel_ctor` @ `0x3e730`, `UiPanel_FreeLines` @ `0x3e8c8`, `UiPanelMsg_Draw` @ `0x3e94c`, `UiPanel_WrapText` @ `0x37144`, `UiPanel_SetLineMeta` @ `0x3f06c`, `UiPanel_SetBytes0to2` @ `0x3f05c`, `UiPanel_SetBytes3to5` @ `0x3e93c`, `UiPanel_SetBytes6to11` @ `0x3e914`, `IncDwordIfInRange_AtPlus0xBD4` @ `0x3f00c`, `DecDwordIfPos_AtPlus0xBD4` @ `0x3f040`, `UiPanel_ScrollNext` @ `0x99f78`, `ScrollPrev` @ `0x99f88`, `UiScrollList_TrackHold` @ `0x3ed44` |
| `game/pufo_font.{h,c}` | see `docs/parity/game-core.md` (the text layer is shared, so it lives with the map/entity types) |

## The `UiPanel` size is 7092, proven three ways

The allocation size is explicit in the code at a `UiPanel_ctor` call site:

```
0x3c750  MOV  R0, #0x1BB4   ; size = 7092
0x3c754  BL   ??2@YAPAXI@Z  ; operator new(0x1BB4)
```

The layout is then confirmed independently, which matters because two functions in
the binary use a *different* structure under the same name (next section):

| Proof | Result |
|---|---|
| `gUiRootPanel` + `apLines`(0xBE4) + 4*105 | `0x159618` = `nGameTimeSec` @ `0x159618` |
| `gUiRootPanel` + `apLines`(0xBE4) + 4*106 | `0x15961C` = `bGeoscapeFlag_15961C` @ `0x15961C` |
| `UiScrollList_TrackHold` @ `0x3ed44` dword indices | `[754]`→`nUnk3016`, `[755]`→`pfnClick`, `[757]`→`nUnk3028`, `[759]`→`nLineCount`, `[760]`→`nStyle`, `[1762..1767]`→`nPosY`/`nUnk7052`/`nUnk7056`/`nViewW`/`nViewH`/`nFont`, `[1771]`→`nUnk7084` |

The first two are the important ones: the clock globals are reached **through**
`apLines[105]` and `apLines[106]`, so those slots are reused as a packed timestamp
and a pause flag rather than as line pointers.

## Resolved: a second struct merged under the name `UiPanel`

Four functions do **not** operate on the 7092-byte panel:

| Function | Line stride | Line flags | Count |
|---|---|---|---|
| `UiPanel_InsertLine` @ `0x5a294` | 16 bytes | `u8 abLineFlags[n]` at offset 0 | `u8` at `+0xD48` (3400) |
| `UiPanel_RemoveLine` @ `0x5a3dc` | 16 bytes | same | same |
| `UiPanel_GetLineFlag` @ `0x5a334` | — | reads `*(u8*)(base + nLine)` | — |
| `UiPanel_GetLineColor` @ `0x5a33c` | 16 bytes | 4 dword colour slots at `+0xC8..+0xD4` | — |

`InsertLine`, `RemoveLine` and `GetLineFlag` all index the line as a **plain byte
offset** at offset 0 and the record as `16 * nLine`; `GetLineColor` reads
`base + 16*nLine + 200 + 4*slot`. That is self-consistent and cannot describe the
7092-byte panel, whose line storage is the `apLines[500]` pointer array at `0xBE4`
with 4-byte stride.

So IDA merged two types. **These four functions are deliberately not ported** as
panel members; they need the second structure recovered first (find its allocation
size at a call site, then derive the fields). This is recorded in the IDB as a
comment at `0x5a294` and `0x3e94c`, including the `gUiRootPanel` clock proof, so the
next pass starts from the resolution rather than from the ambiguity.

## Verified behaviours

| Behaviour | Evidence | Test check |
|---|---|---|
| Layout: `gap_0` @0, `aLineMeta` @0x10, `nUnk3016` @0xBC8 | `UiPanel_ctor` field writes | absolute offsets asserted; the scalar run `bUnk3024..nStyle` asserted by relative order on both targets |
| Defaults: `nFont` 17, `bFlag7072` 1, `bFlag7073` 1, `nStyle` 1, `nViewW` 240, `nViewH` 320, `bUnk7088` 0xFF | `UiPanel_ctor` @ `0x3e730` | all asserted |
| Line buffers are 50 bytes and start empty | ctor allocates `0x32` per line | asserted |
| `SetBytes0to2` / `3to5` / `6to11` write `gap_0` in order | the three setters | each slot asserted |
| `SetLineMeta` writes 6 bytes per line in order | `UiPanel_SetLineMeta` @ `0x3f06c` | two RGB triples read back in order |
| `ScrollNext` clamps at `nLineCount - nStyle` | `IncDwordIfInRange_AtPlus0xBD4` @ `0x3f00c` | 20 scrolls stop at 7 with 10 lines / style 3 |
| `ScrollPrev` clamps at 0 | `DecDwordIfPos_AtPlus0xBD4` @ `0x3f040` | asserted |
| A style larger than the line count keeps the cursor at 0 | the limit goes negative | asserted |
| Wrap produces lines no wider than the limit, splitting on spaces | `UiPanel_WrapText` @ `0x37144` | all produced lines non-empty; short text is one line; empty input gives zero lines; a start line offsets the writes |

## Bugs found and fixed during this task

**`WrapText` wrote through the `nLineCount`-gated setter.** `UiPanel_SetLineText`
gates on `nLineCount` (correct for a caller asking "does this line exist?"), but
`WrapText` *rebuilds* `nLineCount` as it goes. Writing line 3 while `nLineCount` was
still 1 was therefore silently refused, which dropped the last word and left an empty
trailing line. Fixed by splitting out a capacity-gated `UiPanel_SetLineRaw` for the
wrap path. Symptom was "wrapped 8 lines, last one blank, `theta` missing".

## Deviations, each with its reason

| Deviation | Reason |
|---|---|
| `UiPanel_ctor` clamps `nLineCount` to the 500-line capacity | the original trusts its caller and would walk past `apLines`; the clamp turns an overflow into a shorter panel |
| Line text writes are truncated to 49 bytes | the original `strcpy`s into a 50-byte buffer, so an over-long string overflows into the next line pointer |
| The `apLines[105]`/`[106]` clock aliasing is not modelled | on ILP32 those slots *are* `nGameTimeSec` and `bGeoscapeFlag_15961C`; on Win64 the entries widen so the alias cannot hold. The clock lives in `GameClock` (see `pufo_tactctl.h`) instead, keeping the two identities separate |
| `UiPanel_DrawText` renders text rows only | the original first blits a background strip and source rect via `SoftSurf_BlitRectProxy` / `SoftSurf_BlitSrcRect`, whose entry points are not ported yet. The row placement and alignment logic (the part the panel owns) is complete |
| `WrapText` does not reproduce the trailing space appended to the working copy | the decompiled index arithmetic for that loop did not survive cross-checking (a literal transcription emitted a line containing only the space). The observable contract — lines no wider than the limit, words not split — is implemented and tested instead. **This is an open deviation**: in the exact-fit case the original may emit one more line than the port |

## Checks run

| Check | Result |
|---|---|
| `cmake --build build --config Release` | 0 errors, 0 warnings (`/W4 /WX`) |
| `build/tests/Release/test_panel.exe` | **PASS, 0 failures** |
| full suite | 14/14 suites **PASS** |

## Not yet recreated, with reason

| Function | Size | Why not yet |
|---|---|---|
| `Ui_DrawBaseInfo` @ `0x9b5f4` | 10,196 | base screen drawing; needs the ported `UiPanel` draw plus the base data model |
| `Ui_TextInputDialog` @ `0x632cc` | 6,096 | modal text entry; needs keyboard input routing, which is `tact-ui` |
| `UiModal_DrawDialog` @ `0x264a0` | 2,676 | modal frame; needs the background/source-rect blits noted above |
| `Ui_ShowTError` @ `0x996e4` | 2,196 | only reachable from the strategy screens |
| the `UiPanel_InsertLine` family | — | **blocked by the struct conflict above** |

## UiControl button widget (done)

Files: `src/pufo/ui/pufo_control.{h,c}`. Evidence: `UiControl_ctor` @ 0x1a28c,
`UiControl_FreePads` @ 0x1a2c8, `UiControl_BlitActivePad` @ 0x1a340,
`UiControl_SetPosMode` @ 0x1a5c0, `UiControl_Setup` @ 0x1a75c, `EmptyBtn_OnClick`
@ 0x1a228.

### Layout (32 bytes, confirmed by `type_inspect`)

    0x00 x i16   0x02 y i16   0x04 uWidth u16   0x06 uHeight u16
    0x08 nMode   0x09 bEnabled  0x0A nState      0x0C pfnClick
    0x11 bUnk17  0x14 pPad0     0x18 pPad1        0x1C pPad2

`apPads[3]` are the three visual states, so `nState` indexes them directly. The struct
is pointer-heavy and grows on Win64; only field order and the mode/state semantics need
to match, and this widget is never serialised.

### The mode/state machine

    disabled  -> draws nothing
    mode 0    -> pad index = nState            (no clamp)
    mode 1    -> pad index = min(nState, 2)    (CLAMPED)
    mode 2    -> draws nothing (inert mode)

The clamp is the load-bearing detail: `nState` is a `u8` but only pads 0..2 exist, so
mode 1 guards against reading past `pPad2`. **Mode 0 does not clamp** � the original
trusts its caller there, and the port reproduces that asymmetry rather than adding a
clamp the original does not have.

Before drawing, the control writes its own `x`/`y` into the pad's origin fields, so a
pad's position always follows its control. `bUnk17` selects between `PadImage_Blit`
@ 0x110dc and `PadImage_BlitEx` @ 0x11110.

### SetPosMode

Adopts pad 1's size in **every mode except 2**, so mode 2 is the one mode that keeps
the control's current size. `nState` is reset to 0.

### Deviations

| # | Deviation | Why |
|---|---|---|
| 1 | `UiControl_FreePads` NULLs each pad pointer as it frees | The original leaves them dangling, so a second call double-frees; a double free is undefined rather than defined behaviour |
| 2 | An unknown `nMode` draws nothing instead of falling through the switch | The original's switch has cases 0..2 only; an unknown value is not a modelled state |
| 3 | `UiControl_Setup`'s pad loading is injected | It reads the asset archives; injection lets the positioning be tested without assets |
| 4 | `EmptyBtn_OnClick`'s draw-mode store is injected | That global belongs to the renderer |

### Modal dialogue state (done)

`UiModal_Begin @ 0x26430` and `UiModal_End @ 0x2648c` are recreated in
`ui/pufo_modal.{h,c}` over a `UiModalState`. `Begin` clears the dword at pCtx+48
and `anActionPhaseSave0[112]`, sets the `apUiSlotsA[19]` low byte (the open flag),
stores the four rect/label ints into slots 12..15, the two callbacks into slots
16 and 18 and the extra argument into slot 17. `End` clears only the open flag,
leaving the stored fields intact. The original takes the implicit context; the port
takes a `UiModalState *`. Covered by `tests/test_modal.c`.


### Panel click-row and line swap (done)

`UiScrollList_ClickRow @ 0x3eee6` and `Ui_SwapStringSlots @ 0x6aaac` are recreated
in `ui/pufo_panel.{h,c}` as `UiPanel_ClickRow` and `UiPanel_SwapLines`.
`UiPanel_ClickRow` computes the clicked row as `(nY - nPosY) / nFont`, stores it in
`nUnk3028` and the dword at panel offset 12, clears `nUnk3016`/`bUnk3024`/the global
`unk_158018`, and when `bFlag7073` is set and the click is inside the
(`nUnk7052`,`nUnk7056`) rect sized `nViewW` x `nViewH`, sets
`nUnk3032 = nUnk3028 + row` and calls `pfnClick`. `UiPanel_SwapLines` swaps two
lines' text and `anLineAux` values through a 200-byte temp (bounded in the port).
Covered by `tests/test_panel.c` (`TestClickRow`, `TestSwapLines`).

## 2026-10-11: Input_OnPointer @ 0x13c6c — the mouse handler

Ported in `src/pufo/game/pufo_input.c`, asserted by `test_input` (`TestPointer`,
48 checks). The first argument is **`pBank` @ `0xAC6D0`**, the widget bank the window
procedure owns: a `UiControl` array at stride `0x20`, the escape-flag byte at
`+0x1042` (`byte_AD712`) and the last pointer position at `+0x1044` / `+0x1048`.
The port models it as `UiWidgetBank` in `ui/pufo_control.h` with the same indices.

### Widget indices per screen (all `pBank + index * 0x20`)

| Screen | Widgets | Screen | Widgets |
|---|---|---|---|
| 0 | 0..18, or only 2, 7, 9, 15 while `Map+0x02` is set | 7 | 53..59 |
| 1 | 19..31 | 12 | 60..88 |
| 2 | 98..100 | 15 | 89..97 |
| 3 | 32..43 | 4 | 101, then 102..103 |
| 5 | 44..52 | 6 | 104 |
| 10, 11 | 105..112 | 13 | 128 |
| 19 | 129 | 8, 9, 14, 16..18 | none |

### Facts the disassembly settled

- **`anActionPhaseSave0` is a byte array at `Map+0x74`.** Five indices are read
  through named globals: `[93]` = `0xD1`, `[95]` = `0xD7`, `[96]` = `0xD8`,
  `[97]` = `0xD9`, `[108]` = `0xE4`. That also means `apUiSlotsA[5]` (the dword at
  `Map+0x74` that `UiControl_HitTest` clears and this function's HUD edge flags
  write) is `anPhaseSave0[0..3]`.
- **`MapView_FocusOnUnit @ 0x2c758` takes a `MapCell *`, not a unit.** It reads the
  cell's `nX` (+0x1A), `nY` (+0x1B) and `nZ` (+0x1C), recentres the camera scroll,
  and sets the cursor layer to `nZ + 1`. The name is wrong in the IDB.
- The multi-target list lives on the **unit**: `Soldier.nMultiTarget` (i8 @ `+0x1A`)
  and `Soldier.multiOrAnim.apMultiTargets[]` (`+0x1C`), not on `Map`. A row click
  focuses the camera on that unit's `pCell`.
- Row base is `283` with the deploy panel **open** and `300 - pHudPad->uHeight`
  closed (`0x140E4`, where `LDRHEQ` fires on the *closed* test); the HUD band is
  `303` open and `320 - uHeight` closed (`0x14130`).
- Screen 17 is not widgets: it clears `unk_156AB0[0..6]` and marks the row bands.
  Modes 0 and 1 are the same four rows at `x 50..190`, `y row+115 .. row+133`;
  mode 2 is seven rows at `x 24..216`, `y row+65 .. row+83` plus `unk_1571B4`.

### Hex-Rays errors found here (four more in the same class)

| Pseudocode | Reality |
|---|---|
| `v8 >= &result->aQueue[1].nArg + 3 && v8 <= &result->aQueue[1].gap_34[1]` | `y >= row + 115 && y <= row + 133` |
| `bFireStatus0[2]`, `bTrajPad27[2]`, `bFireStatus0[1]` | `Map+0x52`, `Map+0x1406D`, `Map+0x51` |
| `gap_60[0]` | `Map+0x3C` (`unk_1139F4`) |
| `nTrajFront0[4]` / `[5]` in the cursor call | the two signed scratch bytes `Map+0x4A40` / `Map+0x4A41` |

### Deviations

| # | Deviation | Reason |
|---|---|---|
| 1 | Returns `void` | the original's return value is ignored by both callers in `PocketUFO_WndProc` (`0xA07FC` falls straight through to storing `unk_AD710`) |
| 2 | `pHudPad`, `pSelUnit` and the multi-target entries are null-checked | the original dereferences them unconditionally |
| 3 | The drag-start scratch reads 0 when no drag is live | the original reads uninitialised stack (`var_4`) there |
| 4 | `pfnFocusOnCell`, `pfnHitTestTactItem`, `pfnInvClearHitSlot`, `pfnInvHandlePointer` and `pfnGeoscapePointer` are injected | those subsystems are not ported yet; the injection matches the existing `TactInputHooks` design |

**Closed item:** `MapCursor_SetPos @ 0x27014` reads the byte at **`Map+0x02`**, proven at
`0x2723C` (`LDRB R3, [R4,#2]` then `CMP R3, #0` / `BNE`), with `R4` confirmed as the `Map`
base by the neighbouring `ADD R3, R4, R3` at `0x27064` producing `Map+0x15334` = `pSelUnit`.
So a large selected unit spreads the cursor footprint over the extra cells only when the
action latch is zero. The port derives `MapCursorPos.bSuppressLarge` from
`TactInputState.nActionLatch` instead of taking it from the caller, and the byte is named
`Map_bActionLatch` in the IDB.


## 2026-10-11: UiControl_HitTest @ 0x1a3d8 — the pointer press/release handler

Ported in `src/pufo/ui/pufo_control.c`, asserted by `test_control` (`TestHitTest`).

### The signature Hex-Rays got wrong

The original takes **five** arguments. The ARM prologue reads a byte from the stack
(`LDRB R0, [SP,#arg_0]` @ `0x1A3F0`) and the mode dispatch reads `R3`
(`ANDS LR, R3, #0xFF` @ `0x1A42C`). Hex-Rays rendered both as uninitialised locals
(`v3`, `v15`), which made the function look like it tested a random stack byte.
Applied signature:

```c
int __fastcall UiControl_HitTest(UiControl *pCtl, int x, int y, char bDown, char bForceClick);
```

`Input_OnPointer` is the only caller (12 call sites) and always passes `bDown = 1`
(`MOV R3,#1`) and `bForceClick = 0` (`STRB R11, [SP,#var_8]` with `R11 = 0`).

### Behaviour, with the address of each rule

| Rule | Evidence |
|---|---|
| `bEnabled == 0` returns 0 immediately | `0x1A3E0` (`[R4,#9]`) |
| `bForceClick && nMode == 0 && nState == 1` -> tail-call `pfnClick`, return 1, no position test | `0x1A3F0..0x1A428` |
| The hit box is measured **only on a press**; a release never consults it | `0x1A438` (`BEQ` past the box when `bDown == 0`) |
| Box is `x >= ctl.x`, `y >= ctl.y`, `x < ctl.x + uWidth - 1`, `y < ctl.y + uHeight - 1` — the far edge is exclusive one pixel early | `0x1A43C..0x1A480` |
| Press, mode 0 or 2: hit and state 0 -> state 1 + redraw; miss and state 1 -> state 0 + redraw; every path in this branch clears `Map+0x60` | `0x1A4F8..0x1A538` |
| Press, mode 1: hit and state < 2 -> `state += 2` + redraw; miss and state > 1 -> `state -= 2` with **no** redraw | `0x1A4B0..0x1A4F4` |
| Press, any other mode: return the hit, change nothing | `0x1A4A8` |
| Release, mode 0 or 2 with state 1 -> state 0, redraw, click, return 1 | `0x1A558..0x1A584` |
| Release, mode 1 with state > 1 -> `state = (state == 2) ? 1 : 0`, redraw, click, return 1 | `0x1A588..0x1A5B4` (`STRBEQ R6` / `STRBNE R7`) |
| Release, any other mode or a state that does not qualify: return 0 | `0x1A554`, `0x1A560`, `0x1A590` |

`Map+0x60` (IDB `unk_113A18`, Hex-Rays' `apUiSlotsA[0]`) is now commented as the slot
this function clears on every mode 0/2 press path.

### Deviations

| # | Deviation | Reason |
|---|---|---|
| 1 | `pCtl` and `pCtx` are null-checked | the original dereferences both; the port must not crash on an uninitialised widget |
| 2 | `pnUiSlot0` may be NULL, in which case the slot clear is skipped | the widget lives in the UI layer, which has no context object yet; the caller that owns `Map` passes the real pointer |
| 3 | `pfnClick` is called normally instead of as a tail call | same observable behaviour; a tail call would also let the handler's return value become this function's result, which the original does not intend |


## 2026-10-11: Input_OnPointerUp @ 0x148d0 — the release pass

Ported in `src/pufo/game/pufo_input.c`, asserted by `test_input` (`TestPointerUp`, 68
checks). 2140 bytes, 19 jumptable cases.

### What it actually is

Every `UiControl_HitTest` call in it passes `x = 0`, `y = 0`, `bDown = 0` and
`bForceClick = 0` — `MOV R1,#0; MOV R2,#0; MOV R3,#0; STRB R8,[SP,#bForceClick]` at
`0x14A60..0x14A70`, with `R8 = 0` set in the prologue. Because `UiControl_HitTest` only
consults the hit box on a press, the coordinates are irrelevant: this function releases
every control the current screen owns, and that release is what fires their `pfnClick`.

### Facts proven from ARM

| Fact | Evidence |
|---|---|
| Prologue clears the four HUD edge **bytes** `Map+0x74..0x77` individually, then stores the dword `200` into `apUiSlotsA[4]` (`Map+0x70`) | `0x148E4..0x148F8` (`MOV R0,#0xC8; STR R0,[R4,#…]`) |
| `Input_OnPointer` clears `apUiSlotsA[4]` on a drag; `Input_OnPointerUp` **sets** it to 200 unconditionally | `0x14534` vs `0x148F8` |
| Gate is `bTactFlag` and (`CanScroll` **or** `Map_bTickHold @ Map+0x5A`) — unlike the other handlers it runs while the queue is busy | `0x14B18..0x14B64` |
| Screens 16, 17 and 18 are answered **before** the gate; 16 runs `Titles_Shutdown` + `GameUI_InitStartup`, 18 runs `EndScreen_Shutdown` | `0x14924`, `0x14AF0`, `0x14B08` |
| The screen-17 list controls are at `pBank + 0xE20`, i.e. bank index **113 + row** | `0x14A5C` and `0x14AB8` (`ADD R0, R6, #0xE20; ADD R0, R0, R4, LSL#5`) |
| Row block by mode: 0 -> rows 0..3 (113..116), 1 -> rows 4..7 (117..120), 2 -> rows 8..14 (121..127) plus `unk_1571B4` | `0x14A58`, `0x149E8`, `0x1494C` |
| Mode 0 clears only the rows **before** the hit one, and clears the hit row only when `g_bListRowSticky` (`unk_188D4C`) is clear; modes 1 and 2 always clear every mark | `0x14A94..0x14ADC` |
| While `Map+0xAC` is live the hover column at `Map+0xC0` decides: 0 clears it, 1 and 2 run `UiModal_End` then the stored action pointer at `Map+0xA0` / `Map+0xA8` | `0x14B68..0x14BE4` |
| Screen 3 re-derives the action latch from `Map_nActionMode` (`Map+0x4C`): 0..2 -> 2, 11 -> 1, 12 -> 0, 8 -> 0 unless `pSelWeapon->pObj->uSortKey == 23` | `0x14F64..0x14FBC` |
| The action TU cost table is a **dword array at `Map+0x4E4C`** indexed by mode: `MOV R2,#0x4E00; ORR R2,R2,#0x4C; ADD R3,R4,R1,LSL#2; LDR R3,[R3,R2]` at `0x14FC8..0x14FE4`. `Ui_FillActionCosts @ 0x263F0` writes it at dword index `mode + 0x1393`, which is the same address (`4 * 0x1393 = 0x4E4C`) | both sites |
| An unaffordable mode pushes text record 1 ("not enough time", `aTextRecs + 0xCD`), sets mode 12 and clears the latch | `0x15008..0x15024` |
| Only mode 3 (throw) and mode 8 (the target modal) keep screen 3 open; everything else clears `nUiScreen` | `0x15048..0x1505C` |
| Screens 20 and 21 adopt the low word of `apUiSlotsA[11]` (`Map+0x8C`) | `0x14C50` |
| The inventory drag flag here is `Map+0x3C` (`unk_1139F4`), not `Map+0x60` | `0x150BC`, `0x15120` |

### Hex-Rays rendering errors corrected here

- `MEMORY[0x1139B8].gap_2[1]` is `Map+0x02`, the action latch — the same rendering error as
  in `Input_OnConfirm` and `Input_OnSecondary`.
- `nTrajFront0[4 * nActionMode + 1040]` is not that array. The ARM reads `Map+0x4E4C +
  4 * nActionMode`. The earlier note in `pufo_tactui.h` quoting `nTrajFront0[4*m + 1040]`
  is a pseudocode artifact; the port's `anActionCost[12]` is the right model.
- `gap_60[0]` in the screen-1 case is `Map+0x3C`.
- `MEMORY[0xAD4F0][i]` is not a pointer stored in memory; it is `pBank + 0xE20` indexed as a
  `UiControl` array, which the ARM shows as two `ADD`s.

### Deviations

| # | Deviation | Reason |
|---|---|---|
| 1 | `pfnClick` is called normally instead of as a tail call, and with no argument | the original tail-calls it and leaves a leftover register in `R0`; no handler reads it |
| 2 | `pSelWeapon`, `pObj` and `pSelUnit` are null-checked in the screen-3 path | the original dereferences all three; the port must not crash on an unarmed action pad |
| 3 | `anActionCost[n]` is only read for `n` in 0..11 | the original indexes it for any mode value, including negative ones, which reads outside the table |
| 4 | `Titles_Shutdown`, `GameUI_InitStartup`, `EndScreen_Shutdown`, `Geoscape_OnPointerUp` and the two stored action pointers are injected | those subsystems are not ported yet |

## 2026-10-11: the tactical HUD action buttons

Ported in `src/pufo/game/pufo_tacthud.c`, asserted by `test_tacthud` (50 checks).
These are the `pfnClick` handlers `Tact_InitUiControls @ 0x3b498` installs on the
tactical action pads (`TactHud_ToggleKneel` is referenced twice from there, at
`0x3B518` and `0x3BD3C`), so they are reached through `UiControl_HitTest`, not
through a key.

### The unit-type record has four bytes, not one

`g_aUnitTypeRecord @ 0xA7B80` (renamed from `byte_A7B80`) is 24 records of four
bytes, indexed by `Soldier.nUnitType`. The port already had two of them as flat
tables, and both were checked against the binary again:

| Byte | Set for unit types | Who reads it |
|---|---|---|
| 0 | 3, 7, 8, 11, 13, 18 | `TactHud_ClimbUp @ 0x16798` (`LDRB R3, [R0,R3,LSL#2]`, `CMP R3,#1`) skips its ladder test when set |
| 1 | 4, 5, 6, 7, 8, 18, 20, 21 | `Unit_bIsLarge @ 0x49ecc` (`g_aUnitTypeIsLarge`) |
| 2 | 0, 1, 2, 3, 9, 10, 11, 12, 13 | not read by these three handlers |
| 3 | 14, 15, 16, 17, 19, 20 | not read by these three handlers |

`TactHud_ClimbUp` tests byte 0 and `Unit_bIsLarge` tests byte 1 of the same
record, so the two are not interchangeable.

### Kneel is the marksmanship bonus

`TactHud_ToggleKneel @ 0x16570` spends 2 TU, flips `Soldier.bFlags` bit 0, and
pushes one of two `aTextRecs` records. The ARM loads the record addresses
directly (`LDRNE R3, =0x506DB` at `0x1661C`, `LDR R3, =0x5060E` at `0x16654`),
and both are exact multiples of the 205-byte stride:

| Record | Offset | Text (lang.dat, Windows-1251) | Meaning |
|---|---|---|---|
| 1606 | `aTextRecs + 0x5060E` | `Marksmanship increased by 25%` | pushed when the unit kneels |
| 1607 | `aTextRecs + 0x506DB` | `No marksmanship increase` | pushed when it stands back up |

So the message is about the shot bonus, not the pose. The gate, in ARM order:
`nUnitType <= 3`, the unit's cell has a floor sprite, `uTu >= 2`, and
`Unit_bArmorTuWarn(2)` is clear. The click sample 17 plays before any of that,
so it is audible even when the action is refused.

`nAnimPose` is moved by hand (`+1` standing, `-1` kneeling) and
`Unit_UpdateAnimPose` is **not** called, unlike the queue's stand-from-kneel
command 7, which does. Nothing is queued: the pose changes immediately.

### The climb ladder test has two halves

`TactHud_ClimbUp @ 0x1674c` and `TactHud_ClimbDown @ 0x16af0` are mirrors, and
the pseudocode hides the layer arithmetic behind Hex-Rays pointer rendering.
The ARM bases are unambiguous:

| Base | Value | Means |
|---|---|---|
| `R8` | `0x15338` | `Map.apCells`, the unit's own layer |
| `R7` | `0x18B78` | `apCells` at `nZ + 1` |
| `R8` (reloaded) | `0x18B7C` | layer + 1, `x + 1` |
| `R9` | `0x18C68` | layer + 1, `y + 1` |
| `R7` (reloaded) | `0x18C6C` | layer + 1, `x + 1`, `y + 1` |

Climb up, in order:

1. Unless `g_aUnitTypeRecord[type][0] == 1`, both the unit's own cell and the
   cell one layer up must have a floor sprite whose blocking byte `0x2E` is set
   (`MapCell_TestFlag` selector 6) — `0x167A4..0x16808`.
2. `nZ + 1 <= 3` (`0x16814`).
3. If the upper cell has a floor sprite (selector 5), both floors must block
   again — `0x16840`, `0x16874`, `0x168A8`. This half only bites when step 1 was
   skipped by the type flag; otherwise it repeats a test that already passed.
4. The upper cell must not be blank (selector 0) and must have no occupant.
5. For a large unit, the three extra tiles **of the upper layer** must have no
   wall facing the climb cell — `x+1` wall A, `y+1` wall B, `x+1,y+1` against
   both — and no occupant (`0x1696C..0x16AB8`).
6. `TactState_EnqueueVertStep(pSelUnit, 1)` then `TactState_KickQueue`.

Climb down is the same with `nZ - 1` and no type-flag gate: if the unit's own
cell has a floor sprite, both it and the cell below must block. `nZ - 1 < 0`
returns on the signed test at `0x16B18` (`SUBS` / `BMI`). The facing byte is
`0xFF`, which `MoveVert` reads as `-1`.

Neither handler spends TU. The 8 TU cost is charged later, when the queue
reaches command 5.

### Deviations

| # | Deviation | Reason |
|---|---|---|
| 1 | `pSelUnit`, `pCell` and every looked-up cell are null-checked | the original indexes `Map.apCells` directly and never checks; the port's lookup returns NULL out of range |
| 2 | Climb up returns when the layer-1 lookup is out of range instead of reading past the pointer table | the original reads `apCells[4]` for a type-flag unit on layer 3, which is past the end of the array |
| 3 | A neighbour lookup that returns NULL skips that neighbour's test | the original would wrap to the next row of `apCells` for a unit on the map edge |
| 4 | The dead second `pFloorSpr == 0` test in the kneel branch is not reproduced | it is unreachable: the gate at `0x165A4` already proved the sprite exists |
| 5 | `Sound_Play(17)` is injected as `pfnPlaySample` | the port's audio seam has no mode argument; the original passes mode 4 |

