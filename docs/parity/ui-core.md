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

## `Tact_OpenLeftHandMenu @ 0x17030` / `Tact_OpenRightHandMenu @ 0x17148` — the hand action menus

Ported as one function, `Ui_OpenHandMenu` in `src/pufo/game/pufo_tactui.c`, asserted by
`TestHandMenu` in `tests/test_tactui.c`. The two originals are the same 280-byte body
with a different hand slot read (`pHandL` at `Soldier+0x104`, `pHandR` at `+0x108`), so
the port has one implementation and the caller says which hand it picked.

### Gate order, ARM-proven at `0x1703C..0x170B0`

| Step | Address | Evidence |
|---|---|---|
| sample 17, mode 4 | `0x1703C` | `MOV R0,#0x11 / MOV R1,#4` |
| bail when the action latch is armed | `0x1704C` | `LDRB` of `Map_bActionLatch` (`Map+0x02`), `CMP #0` |
| `pSelWeapon = pHandL`, bail when NULL | `0x1705C` | `LDR R3,[R0]` after the hand load, `CMP #0` |
| `nSortKey = pObj->uSortKey` | `0x17074` | `LDRH R0,[R3,#0xC8]` |
| bail when the research record says no menu | `0x17078..0x17094` | `LDRB R3,[R0,R1]` on `byte_AA968`, `MUL LR,R3,#0x22`, `LDRB R3,[R3,#0xD]` |
| bail when `nSortKey == 24` and `Soldier+0x1A6 == 0` | `0x17098..0x170B0` | `CMP R0,#0x18`, `LDRB R3,[R2,#0x1A6]` |
| `Ui_FillActionCosts` | `0x170B8` | direct call |
| scan 12 modes | `0x170D0..0x17108` | see below |
| none affordable → push text record 1 | `0x1710C..0x1711C` | `ADD R1,R0,#0xCD` on `aTextRecs @ 0xB38D0` |
| affordable → `nActionMode = 12`, `nUiScreen = 3` | `0x17134..0x17140` | `STR` to `Map_nActionMode`, `STRH` to `Map+0x3A` |

### The two action tables, settled by literal immediates

The scan at `0x170D0` is the cleanest proof of both bases, because the compiler
materialised them as plain constants instead of Hex-Rays' `nTrajFront0[4*m + 1040]` rendering:

```
0x170D0  MOV R3, #0x4E7C          ; enabled table
0x170D8  LDR R3, [R0,R3]          ; Map + 0x4E7C + 4*m
0x170E4  MOV LR, #0x4E00
0x170EC  ORR LR, LR, #0x4C        ; 0x4E4C
0x170F0  LDR LR, [R0,LR]          ; Map + 0x4E4C + 4*m  (the TU cost)
0x170E8  LDRH R3, [R2,#6]         ; pSelUnit->uTu
0x170F8  BLE loc_17134            ; cost <= uTu  -> accept
0x17104  CMP R1, #0x30            ; twelve four-byte steps
```

So the enabled table is `Map+0x4E7C + 4*m` and the cost table is `Map+0x4E4C + 4*m`.
This corrects an earlier note that put the enabled table at `Map+0x4EBC`: `4*0x139F =
0x4E7C`, and `Map+0x4EBC` is a different array entirely — the `Combat_ResolveShot` hit
windows, built with `MOV R0,#0x4E00 / ORR R0,R0,#0xBC` and indexed `4*(41*row+col)+slot`.
The two share the `0x4E00` page, which is how they were conflated.

The scan is a yes/no, not a search for the cheapest or the intended action: the mode it
finds is thrown away and only `nActionMode = 12` is stored. That is why `Input_OnPointerUp`
case 3 later re-derives the real mode from `nActionMode`.

### Deviations

| # | Port | Original | Reason |
|---|---|---|---|
| 1 | The research gate is injected as `pfnWeaponHasMenu(uSortKey, pUser)` | reads `aFacTypeTable[13 + 34*byte_AA968[uSortKey]]` | `aFacTypeTable @ 0x15EF08` is built at runtime (the shipped image is all `0xFF`) and its in-memory field order is not yet recovered; guessing it would make every weapon either fireable or not. `byte_AA968`'s extent is also unpinned (see `docs/parity/game-core.md`) |
| 2 | The mode loop is bounded to the twelve modelled modes | `R1` advances by 4 with no bounds check on the table | the ARM stops at `R1 == 0x30`, which is exactly twelve steps, so the sets match |
| 3 | `pUi`, `pCtx` and `pCtx->pCost` are null-checked, and a missing gate is a refusal | no checks | the port's calling convention passes these by pointer |

## Correction: `anActionPhaseSave0` is at `Map+0x15078`, not `Map+0x74`

Hex-Rays indexes this byte array by index, and the ARM immediates pin the base:

| Hex-Rays | real byte | ARM site | named global |
|---|---|---|---|
| `anActionPhaseSave0[20]` | `Map+0x1508C` | `0x1480C`, `0x14D0C`, `0x16168`, `0x17014` | `bLookCommitFlag` |
| `anActionPhaseSave0[93]` | `Map+0x150D5` | `0x141A8` (the HUD edge-test gate) | `Map_bScrollByPen` |
| `anActionPhaseSave0[95]` | `Map+0x150D7` | `0x144CC` | `Map_bRunByDblClick` |
| `anActionPhaseSave0[96]` | `Map+0x150D8` | `0x14534` | `Map_bTurnByPen` |
| `anActionPhaseSave0[97]` | `Map+0x150D9` | `0x14548` | `Map_bMenuByPen` |
| `anActionPhaseSave0[108]` | `Map+0x150E4` | `0x1462C`, `0x150A8` | `Map_bInvPanelOpen` |

`0x15078 + 93 = 0x150D5` and `0x15078 + 108 = 0x150E4`, which is the base that makes every
index land on a global that has its own `Map_ToggleFlagXXXX_Play11` handler. The earlier
note that put the base at `Map+0x74` was wrong, and it made the port claim offsets
`0xD1/0xD7/0xD8/0xD9/0xE4` for bytes that live at `0x150D5..0x150E4`. `Map+0x74..0x77` is
`apUiSlotsA[5]`, the four HUD edge flags (`Map_aEdgeFlag0..3`), and it is not an
`anActionPhaseSave0` alias.

One consequence was a real modelling bug: the port had a second field `bPhaseSave20`,
documented at `Map+0x88`, for the byte that `Input_OnConfirm` case 4 already models as
`bLookCommitFlag` at `Map+0x1508C`. Both pointer case-4 gates now read
`bLookCommitFlag`, and the duplicate field is gone.

### `MapView_FocusOnUnit @ 0x2c758` is a cell focus, and it is now ported

`MapView_FocusOnCell` in `src/pufo/game/pufo_tactctl.c`. The second argument is a
`MapCell *`: the callee reads `+0x1A/+0x1B/+0x1C` (`MapCell.nX/nY/nZ`) and every
caller passes a cell, e.g. the tail call at `0x16744` from `MapView_FocusSelected`
passes `Map.pSelUnit->pCell`. It recentres the camera with
`MapView_SetCamOffset(nMapTileW>>1, nMapTileH>>1, 0)`, then takes the cell coordinate
minus the two scratch bytes that call just wrote, read signed (`LDRSB` at `0x2C788`
and `0x2C7A0`), and feeds the pair to `Map_CenterOffsetX @ 0x26fcc` and
`Map_CenterOffsetY @ 0x26ffc`, storing into the camera-scroll pair `0x15130/0x15134`.
It tail-calls `MapCursor_SetFlags @ 0x26f78` with the cell's own layer plus one, so
focusing a layer-0 cell puts the cursor on layer 1. Asserted in `test_tactctl`.

## The inventory button and the special action pad

Ported in `src/pufo/game/pufo_tacthud.c` as `TactHud_OpenInventory` and
`TactHud_UseSpecialItem`; asserted in `tests/test_tacthud.c` (38 new checks, 88 total).

### `Inv_OpenScreen @ 0x16ef4`

The gate order is `0x16EFC` sample gate, `0x16F1C` roster gate, `0x16F34` humanoid
gate. The sample gate only gates the sample: with `LOBYTE(apUiSlotsA[2]) @ Map+0x68`
clear the button still opens the screen, it just stays quiet.

| Effect | Offset | ARM |
|---|---|---|
| `nInvListSel = 0xFF` (every category) | `Map+0x4A26` | `0x16F44` |
| `nInvFilterType = 0` (ground list row 0) | `Map+0x1A22` | `0x16F54` |
| `Inv_BuildVisibleList` | — | `0x16F5C` |
| `pBank+0x2AA = 0`, `pBank+0x30A = 1` | `0xAC97A`, `0xAC9DA` | `0x16F68`, `0x16F70` |
| five filter pads cleared | `0xAC9FA/ACA1A/ACA3A/ACA5A/ACA7A` | `0x16F78..0x16F98` |
| action latch cleared | `Map+0x02` | `0x16FA0` |
| cursor re-stamped at its own position | `Map+0x15128/0x1512C` | `0x16FB0`, `0x16FBC` |
| `Unit_RecalcInventoryStat` | — | `0x16FD0` |
| `nUiScreen = 1` | `Map+0x3A` (u16) | `0x16FD8` |
| `Map_bDrawIsoLayer = 1` | `Map+0x254B0` | `0x16FE8` |

`Map_bDrawIsoLayer` (was `unk_138E68`) is the tactical-layer draw switch:
`Render_Frame @ 0x22310` skips `MapView_DrawIsoLayer` when it is clear (`0x22404`,
`0x225c4`), and `Tact_InitMission @ 0x3c550` clears it at mission start (`0x3C5C0`).
Renamed in the IDB.

Deviation: the original tests `nXcomUnitCount` but dereferences `pSelUnit` without a
NULL test; the port refuses a NULL selection. Deviation: `Unit_RecalcInventoryStat`
needs the injected object-weight hooks, so a context without them skips the recalc.

### `TactHud_UseSpecialItem20 @ 0x17d54` — the special pad, action mode 8

`nActionMode = 8` (`0x17D74`) and `nUiScreen = 0` (`0x17D78`) are written before the
weapon is read, so pressing the pad with an unusable item still closes the menu screen
and still leaves mode 8 behind. The branch value is `pSelWeapon->pObj->uSortKey`:

| `uSortKey` | Behaviour |
|---|---|
| 20 | pay `anActionCost[8]` off `Soldier.uTu` directly, not through the queue; mark every enemy and every Xcom unit within 10 cells; open `nUiScreen = 6` |
| 21 | the medikit: find a target, refuse it, or enqueue command 18 |
| 23 | affordability only: set the action latch `Map+0x02` to 4 |
| other | stop, with mode 8 and screen 0 already written |

All three gates read the full dword at `Map+0x4E4C + 4*8 = 0x118824` (`0x17DA0`,
`0x17DD8`, `0x18078`). Only the sort-key-20 subtraction truncates: `0x180AC`
`MOV R3,R2,LSL#16 / SUB R3,R1,R3,LSR#16` cuts the cost to 16 signed bits. The cost
handed to the queue at `0x1804C` is the full dword. Failure in any branch pushes
`aTextRecs` record 1, "not enough time".

The mark sweep (`0x180D4..0x18180` enemies, `0x18198..0x18240` Xcom) writes
`Soldier.bSpecialMark @ 0x1B0` from `sqrt(dx*dx + dy*dy) > 10.0`, the double constant
`0x40240000_00000000` at `0x18154`. Exactly 10 cells keeps the mark.

The medikit target search (`0x17E00..0x17F3C`):

- the candidate cell is `pSelUnit->pCell` stepped one `aDirDeltaXY` step in the unit's
  own facing, on the unit's own layer;
- `Unit_CanStepExitDir @ 0x36d68` must say the step is clear, otherwise there is no
  cell target;
- the target is that cell's occupant `MapCell.pUnit @ 0x30`, which may be NULL;
- when it is NULL, the scan looks for a civilian standing on the healer's own cell
  that is not large, is humanoid (`nUnitType <= 3`) and has `uHp != 0`, and the fire
  target becomes the healer's own cell instead of the adjacent one.

Refusals, in ARM order:

1. a non-civilian target that is NULL, inoperative, or large pushes records 9 and 10
   (`0x17F8C`, `0x17FA4`) — "To use a medikit" plus "stand in front of the soldier";
2. a target with `bKneelOrPrimed @ 0xF6` clear that has `uHp == auStat[1]`,
   `bMorale == 100` and `uTuSpent <= uHp >> 1` pushes records 11 and 12 (`0x17FF4`,
   `0x17FA4`) — "Does not need a medikit" plus an empty tail record.

Records 9..12 are `aTextRecs + 205*9 = 0xB4005`, `+205*10 = 0xB40E2`, `+205*11 =
0xB419F` and `+205*12 = 0xB4268`. The pairs are two status lines that read as one
sentence; record 12 is empty in both `main.dat` and `lang.dat`.

Otherwise `nFireTgtX/Y/Z @ 0x14040/0x14044/0x14048` take the target cell and its
layer, `Tact_EnqueueActionOrStatus205 @ 0x4b420` appends command 18 with the target,
`pSelWeapon`, the cost and mode 8, and `TactState_KickQueue` starts the queue.

### `Unit_CanStepExitDir @ 0x36d68` takes a cell, not a unit

Both of its code callers (`0x17C1C` in `TactAction_ConfirmMeleeMode6` and `0x17E3C`
here) load the argument as `LDR R1, [R1,#0x10]` straight off `Map.pSelUnit`, so the
pointer is `MapCell *`. The callee reads bytes `+0x1A/+0x1B/+0x1C`, which are
`MapCell.nX/nY/nZ`. Hex-Rays typed it as `Soldier *` and named the same three bytes
`nMultiTarget`, `gap_1B` and `anim.nPathZ`; the offsets are shared, the intended
record is the cell. The port's parameter is now `const MapCell *`.

Deviations in `TactHud_UseSpecialItem`: the original dereferences
`pSelWeapon->pObj` with no NULL test; the port refuses a missing weapon. The original
indexes `apCells` for the target cell without bounds checks; the port's injected
lookup returns NULL out of range and the search falls through to the civilian scan.
The original indexes the facing delta table without a bound, so facings 8 and 9 (up
and down) read past the eight-row probe table; the port bounds the facing at
`PUFO_PATH_DIR_COUNT` and lets the probe test refuse.


## The widget bank builder: Tact_InitUiControls @ 0x3b498 and Tact_LoadUiPads @ 0x3b068

Ported in `src/pufo/ui/pufo_uibank.c`, asserted by `test_uibank` (388 checks).

### The descriptor record

`UiControl_Setup @ 0x1a75c` names the field offsets in its own ARM at
`0x1a76c..0x1a774`: `xOff` +0, `yOff` +2, `szPad0` +4, `szPad1` +13, `szPad2` +22,
`nMode` +31, stride 32. Each name is a 9-byte inline buffer; an empty name means no
pad, because `UiControl_LoadPads @ 0x1a604` compares each one against the empty
string at `0x10DA38` and leaves that slot NULL. The port keeps the names as
`const char *` and the field order identical.

The thirteen tables were read out of `pufobeta.exe` with `tools/gen_uidesc_c.py`
(`.data` VA `0xA6000`, so `file_offset = ida_va - 0xA6000 + 0x93000`), and every
descriptor decodes to a readable pad name, which is what confirms the layout.

### Groups, pinned by the pfnClick store addresses

Widget index = `(store address - 0xAC6D0 - 0x0C) / 0x20`. That formula pins both the
group boundaries and the handler binding, and the two agree everywhere:

| group | widgets | descriptor | first handler |
|---|---|---|---|
| tactical | 0..18 | `off_A7ED8` | `Tact_CycleNextUnit @ 0x16684` |
| menu column | 53..59 | `unk_A8838` | `Menu_OpenLoadSlots` |
| inventory | 19..31 | `unk_A8158` | `Tact_SelectPrevUnit @ 0x17260` |
| action pads | 32..43 | `unk_A8418` | `Tact_SetFireModeAuto @ 0x17868` |
| deploy grid | 44..52 | `unk_A8918` | `Map_SetSubMode0_Play11 @ 0x18418` |
| options | 60..88 | `off_A8A38` | `MenuBtn_SetSpeed0` |
| research | 89..97 | `unk_A82F8` | `MenuBtn_Secondary` |
| save slots | 98..100 | `unk_A8598` | `MenuBtn_Confirm0` |
| load slots | 101..103 | `unk_A86B8` | `MenuBtn_Confirm1` |
| quit | 104 | `stru_A8718` | `MenuBtn_Confirm3` |
| save-load list | 105..112 | `unk_A8738` | `SaveLoad_Slot0` |
| cancel 0 / 1 | 128 / 129 | `stru_A7CB8` / `stru_A7CD8` | `MenuBtn_Cancel0` / `1` |

115 widgets are laid out and 115 handlers are bound. Widgets 113..127 are in neither
list: they are the screen-17 list rows in the 16-byte gap between the 105..112 group
(ending at `0xAD4EF`) and widget 128 (`0xAD6D0`), and the original never touches them.

The inventory group also settles the filter numbering the port already reproduced:
the pads are laid out `pistolvs, grenvs, ammovs` then `riflevs, rocketvs`, and the
handlers installed on them are `SetFilter0, SetFilter3, SetFilter4, SetFilter1,
SetFilter2`. The "quirk" is the visual layout, not a bug.

Nine of the twelve action pads share position `27,136`. That is the data, not a
decoding error: screen 3 only enables the modes the current weapon offers, so the
overlapping slots are inert.

### Overrides and the HUD origin

Only the tactical group is positioned from the HUD pad. The origin is centred on x
and bottom-aligned on y, and the odd `+1` branch applies to x only here:

```
nOriginX = (uWidth > 240 ? 241 - uWidth : 240 - uWidth) >> 1
nOriginY = 320 - uHeight
```

After the groups the original writes seven fields straight into the bank: widget 16
`100x30`, widgets 17 and 18 `34x53`, `bUnk17` on widgets 14, 15 and 31, and `nState`
1 on widget 10. The three sized widgets are all mode 2, the inert mode that keeps its
own size, which is why the size has to be written by hand.

### Tact_LoadUiPads @ 0x3b068

54 pads in three runs, all through `PadImage_Load`:

- 32 manual pads, names `MAN_0M0..MAN_3F3` from the 9-byte-stride table at
  `0xA90F8`, mode 34, origin forced to `50,55`, stored at `Map+0x199C + 4n`
  (`MOV R6,#0x199C` at `0x3b0B0`).
- 11 named pads: `invv` 1 (`Map+0x100`), `soldier` 8 (`Map+0x15108`), `mscanv` 16
  (`Map+0x104`), `selsold` 34, `sbsold` 16, `ssold` 16, `enemystp` 9, `endmiss` 11,
  `twonone` 15, `minimcur` 16, `dpanelv` 1 (`Map+0xFC`, the HUD pad).
- 11 buttons `b0..b10` (`'b'` + `itoa(i)`), mode 1, origin zeroed, stored at
  `Map+0xCC + 4n` (`LDR R0,[R4,#0xCC]` at `0x3b43C`).

`minimcur` is centred with the full rule, both axes: `(uWidth > 240 ? 241 - uWidth :
240 - uWidth) >> 1` and the same against 320.

### Deviations

1. The original allocates every pad with `operator new(0x14)`. The port takes the
   allocation through an injected hook so a test runs without a heap and the app
   shell can pool it.
2. The original stores the pad pointer even when the allocation failed and then
   calls `PadImage_Load` through the NULL. The port stores NULL, skips the load, and
   reports the failure.
3. The original writes two single bytes between group loops (`nState` on widget 10
   after the menu column, `bUnk17` on widget 31 after the inventory group). No later
   group touches either widget, so the port applies them with the other overrides at
   the end; the observable result is the same.
4. The click handlers are installed from a caller-supplied table indexed by widget,
   because the port's handlers take injected contexts rather than reading globals.
   The original handler for each slot is recorded in `g_aUiClickName`, which is also
   the work list for the handlers that are not ported yet.

## The widget click adapters: MenuBtn_* @ 0x19498..0x1a26c

Ported in `src/pufo/ui/pufo_menubtn.h/.c` as `test_menubtn`, 67 checks.

Fourteen of the handlers `Tact_InitUiControls` installs are not behaviour, they are
adapters: 24..56 bytes each, the widget bank as their only argument, and three
statements. Ported by behaviour rather than per widget, because half of them are
byte-identical duplicates:

| original | bytes | what it does |
|---|---|---|
| `MenuBtn_Confirm0 @ 0x19650`, `Confirm1 @ 0x196e8`, `Confirm2 @ 0x19884`, `Confirm3 @ 0x198a4`, `Confirm4 @ 0x19910`, `Confirm5 @ 0x1a014` | 32 | sample 16, `Input_OnConfirm @ 0x15f3c` |
| `MenuBtn_ConfirmSnd15 @ 0x198f0` | 32 | the same with sample 15 |
| `MenuBtn_ConfirmClear @ 0x194c8` | 48 | sample 16, `nUiScreen = 0`, `Input_OnConfirm` |
| `MenuBtn_Secondary @ 0x19498` | 48 | sample 16, `nUiScreen = 0`, `Input_OnSecondary @ 0x156e0` |
| `MenuBtn_SecondaryDismiss @ 0x195e0` | 56 | the same, then `UiScreen_DismissHigh @ 0x159ec` |
| `MenuBtn_ActionBack0 @ 0x195a8`, `ActionBack1 @ 0x19618` | 56 | sample 16, `nUiScreen = 0`, `Input_OnAction @ 0x1557c`, `UiScreen_OnBack @ 0x15670` |
| `MenuBtn_Cancel0 @ 0x1a24c`, `Cancel1 @ 0x1a26c` | 32 | sample 16, `UiScreen_OnCancel @ 0x16200` |

### The screen clear makes part of the callees dead

Five of them write `nUiScreen = 0` before the handler runs, ARM at `0x194B0`,
`0x194E0`, `0x195C0`, `0x195F8`, `0x19630`:

```
LDR  R0, =0x1139B8
MOV  R1, #0
STRH R1, [R0,#(unk_1139F2 - 0x1139B8)]   ; nUiScreen, Map+0x3A
```

`UiScreen_OnBack` and `UiScreen_DismissHigh` only act on screens 8, 9 and 13, so
from these widgets those branches are unreachable: `SecondaryDismiss` always takes
its no-op path, and `ActionBack`'s quit / battle-abort / cancel paths never run.
`MenuBtn_ConfirmClear` on the options screen does not save the options either; it
lands in the screen-0 case, which with a clear action latch plays sample 7 and
toggles the deploy panel. The port reproduces the order instead of fixing it, and
the test asserts the dead branch rather than the intended one.

### The two scratch bytes

`Input_OnAction` sets `byte_AD711` (pBank+0x1041) and `UiScreen_OnBack` clears the
same byte (`0x15674 MOV R12,#(unk_AD711-0xAC6D0); STRB R2,[R1,R12]`), so
`MenuBtn_ActionBack` leaves it 0. `UiScreen_DismissHigh` clears `bUiEscapeFlag`
(`byte_AD712`) on entry. The port passes both through `UiButtonCtx` so the caller
owns the storage.

### The slot list and the quit dialog

`Menu_OpenLoadSlots @ 0x18864` and `Menu_OpenSaveSlots @ 0x188a4` are identical
except the screen they open: both call `SaveSlot_FormatList @ 0x45904` first, play
sample 16, clear the list-mode byte `*0x157188`, then write 11 for the load list or
10 for the save list. That 10 is what `SaveLoad_Slot0..6 @ 0x19de4..0x19fc4` test to
pick `SaveGame @ 0x44988` over `LoadGame @ 0x44bbc`.

`Menu_ConfirmQuitDialog @ 0x18e2c` is the yes/no modal: sample 16, then
`UiModal_Begin(pCtx, 140, 24, 4, "YES", App_QuitFromTact @ 0x18e18, "NO",
Menu_CancelQuitDialog @ 0x18e04)`, then `nUiScreen = 8`. The slot layout is now
explicit in `UiModalState`: slot 15 holds the first label and slot 17 the second,
so `pLabel15` replaced the fourth int of `anRect12_15`. `App_QuitFromTact` is
`Tact_Shutdown(1) + App_QuitDisplay`, which is the app shell's job, so both
callbacks are injected. `Menu_CancelQuitDialog`, the NO callback, only clears the
screen.

### The options screen re-reads itself: Options_SyncUiFromMap_GotoScreen12 @ 0x188e4

Ported as `Menu_SyncOptionPads` in `pufo_menu.c`. The options button changes no
option. It clears `nState` (byte +0x0A) of all 29 widgets of the block at
`unk_ACE50` and re-derives every one of them from the option block, then opens
screen 12. The widget range is the pad block `MenuPads` now models in one struct:

```
60..64 speed   0xACE5A   32 * nGameSpeed + 10
65..69 gamma   0xACEFA   32 * gamma       + 170
70..74 pen     0xACF9A   flags 93..97, i.e. Map+0x150D5..0x150D9
75     confirm 0xAD03A   cleared, never lit again
76     music   0xAD05A   flag 98  (Map+0x150DA musicoff)
77     sfx     0xAD07A   flag 99  (Map+0x150DB soundoff)
78..82 music   0xAD09A   32 * musicvolume + 554
83..87 sfx     0xAD13A   32 * soundvolume + 714
88     nodrag  0xAD1DA   flag 108, INVERTED
```

The volume groups prove the option values are 1..5, not 0..4: `554 = 32*17 + 10`,
so the pad is widget `77 + musicvolume`, and `Options_SetMusicVol1 @ 0x19A10`
really writes the immediate 1 into `unk_128A98` (Map+0x150E0) while
`Options_SetMusicVol5 @ 0x19B60` writes 5. `MenuBtn_SetSfxVol1 @ 0x19BB4` and
`MenuBtn_SetSfxVol5 @ 0x19D34` do the same at `unk_128A94` (Map+0x150DC). The port
had been storing 0..4 there, which is corrected: the buttons stay 0-based and the
stored value is `nStep + 1`, matching what `Options_LoadFromRegistry` already
clamps to 1..5 and what `Sound_ApplyMusicVolume @ 0x9E454` scales
(`value = muted ? 0 : 12*volume + 4`, `level = 4*value - 1`, so muted gives -1).

Deviation: the original indexes those four pads with no bounds test, so a volume of
0 lights the sfx-mute pad of widget 77 and a speed above 4 writes past its group.
The port only writes inside the group.
