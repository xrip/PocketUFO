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
| 0 | 0..18, or only 2, 7, 9, 15 while `Map+0x22` is set | 7 | 53..59 |
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

**Open item:** `MapCursorPos.bSuppressLarge` is supplied by the caller. The citation
`pMap->gap_2[1]` came from pseudocode; `MapCursor_SetPos @ 0x27014` still needs an
ARM read to confirm whether it is `Map+0x22`.


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

