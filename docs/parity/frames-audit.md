# Frame audit: every test picture against its IDB draw arm

Method (set by the user on 2026-10-23): look at each picture on its own, read the code that draws it in the IDB,
then ask what is missing. A sweep alone is not an audit -- it finds candidates, the IDB reading decides.

## The sweep

Question asked of every picture: is a row black across all 240 pixels (index 0 everywhere)?

First pass was wrong and the bug is recorded here: it compared a bytes slice with a tuple, and
`b'\x00\x00\x00' == (0,0,0)` is never true in Python, so no row looked black and a black row counted as "lit 240".
The corrected sweep over 103 pictures (93 `tests/ref/*.ppm` + 10 live `build/e2e/*.ppm`) found 21 with a band:

| picture | black row runs | status |
| --- | --- | --- |
| geoscape_ufo_enroute | 1..41 | FIXED in Part 156 (fixture scroll outside the 0x837f8 window) |
| geoscape_terror_site | 156..235 | FIXED in Part 156 (same cause) |
| geoscape_descent_panel | 1..59, 156..235 | arm isolation: the frame runs `ClearSurface` + `Geoscape_DrawStatusPanel` only (test_geoscapeframe.c ~1362..1375) -- no map, no strip. Open: is a panel-only frame a useful reference? |
| tact_hud_minimap | 16..63, 317..319 | open -- the radar inset's own area, not the map |
| tact_mission_urban | 1..16 | open |
| tact_briefing, tact_briefing_urban, tact_mission_from_descent | 1..7, 311..319 | open |
| debrief_banner | 1..12, 25..42, 44 | open |
| tact_hud_promotion | 1..12, 25..32 | open |
| tact_hud_inventory, 13_tact_unit_panel | 153..159, 199 | open -- same region in a fixture frame and a live e2e frame |
| tact_hud_markers | 319 | open (1 row) |
| econ_buy_craft_queued, econ_hangar_cap_full, transfer_open, transfer_staged, transfer_committed | 1..2, 29..30, 57..58, 85..87 | open -- the same four thin runs in five pictures of the same panel family |
| 00_mainmenu, 10_menu_at_boot, 11_slot_list | 1..2, 264..268, 313..319 | open |

Nothing else in the corpus has an unfilled row, and no picture has a black gap inside a lit span.

## What Part 156 proved about geoscape frames

- `Geoscape_BlitMapView` (0x8C2B0..0x8C690) wraps X for every screen id; the Y wrap (0x8C418..0x8C444) and the second
  vertical map copy (0x8C4E4) are inside the `CMP R0,#2` arm. Every other id blits the map pad at one origin
  `-(nScrollY + nMapH/2 - 160)`.
- So vertical coverage is guaranteed only by `Smap_ClampScrollY @ 0x837f8` (0x83830 lower, 0x8385C upper; inlined at
  0x82124 Smap_ScrollBy, 0x82080 Smap_ZoomIn, 0x84848/0x84870 Smap_OnPointerUp). Shipped numbers: earth100 800x400,
  strip 84 tall at nOriginY 236, window nScrollY in [-40, 124].
- Consequence for the audit: any geoscape frame whose `nScrollY` sits outside [-40, 124] is not a state the original
  can show, whatever it looks like.

## Open questions, picture by picture

1. `ufopaedia_index` -- the not-dirty arm. `Ui_DrawBaseInfo @ 0x9b5f4` blits the screen pad first (0x9B600), then, only
   when the entry is NOT dirty, the dashed frame (0x9B61C..0x9B7BC -- verified pixel by pixel in the picture: the ramp
   ((i+20)*5 -> 0x62..0xD5 on the near pair, 0xE6-5i on the far pair) is there). The full-screen fill
   (`BlitSrcRect 0,0,240,320 idx 0x64`) belongs to the DIRTY arm, so the map showing through in the index frame matches
   the arm order. Unresolved: what the pad blitted at 0x9B600 actually contains in this state (PadImage_ctor in
   `Ui_InitBaseInfo @ 0x9ddc8`, freed at 0x9B564..0x9B584), and why the selector shows four identical rows.
2. `econ_buy_craft_queued` / `econ_hangar_cap_full` / `transfer_open` / `transfer_staged` / `transfer_committed` -- measured:
   the thing the user saw is one rectangle, x 64..177, y 94..110 (114 x 17), a bright frame (0xAC009D) around a filled
   interior 0x5A0055, different from the panel field around it (0x5A0028/0x5A002C), with zero text-coloured pixels inside.
   Its IDB origin is the buy/sell tail arm: 0x93EB4..0x93F04 is gated on the root block's byte 0x18652D
   (`LDRB R3,[R7,#0x35]`, R7 = 0x1864F8 from Render_Frame at 0x2232C) and, while set, feeds `nSmapCursorX/nSmapCursorY`
   read SIGNED into `UiScrollList_TrackHold` for pBuySellPanelB (0x161A00) AND pBuySellPanelC -- the same cursor position
   for both lists. TrackHold stores `row = (nY - [panel+0x1B88]) / [panel+0x1B9C]`, sets the hold byte 0xBD0 while the
   cursor is inside the panel's box, clears it otherwise. So the box is a hover/hold highlight, not a panel.
   Unresolved: whether the row the cursor maps to is the row the fixture pressed (a fixture-state artifact) or a wrong
   row index in the port -- the highlight sits above the craft list, where the pressed row is not. Next check: the draw
   site that turns byte 0xBD0 into this rect (which panel's geometry, drawn before or after the row text).

   Measured further (a temporary probe in the fixture at the dump moment, since removed): panel A x 10 y 70 font 16
   lines 37 scroll 0; panel B x 180; panel C x 196 -- all three y 70, font 16, 37 lines; the map cursor at that dump is
   (200, 78) and the pressed craft row is 0. The box's x span 64..177 covers none of A/B/C, so the rect is panel E's
   (panel E not yet measured) or comes from another draw entirely.
   Read the rect's own code: `UiPanelMsg_Draw @ 0x3e94c`, block 0x3E9C0. Its blit is gated on panel+0x1BA1
   (bSelLineHighlight) AND panel+0xBD0 (bHoverActive), with x = [panel+0x1B8C]+1 and
   y = [panel+0x1B90] + (panel+0xC, the hover row) * [panel+0x1B9C]. That block is at the HEAD of the function and the
   text pass (Text_MeasureWidth / Text_DrawString) follows it -- in the original a highlighted row keeps its text ON TOP
   of the fill. Our box has no text inside: either the opposite order, or a hover row whose line is empty.
   The port's `UiPanel_DrawText` (src/pufo/ui/pufo_panel.c:330) is text-only, and `BuySellScreen_Draw` calls it for
   A, E, C, B, D (0x78314..0x7833C). So the open point is now exact: (i) whether the buy/sell panel draws in the IDB are
   the whole `UiPanelMsg_Draw` (with the head blit) or its text tail, and (ii) panel E's hover-box fields
   [0x1B8C]/[0x1B90]/[0x1B9C] and its hover row (panel+0xC) at that dump.
3. A live deviation behind the "flat / empty panel rows" look, now pinned from both sides. The IDB: `BuySellScreen_Draw`
   calls **`UiPanelMsg_Draw`** for all five panels at 0x78314..0x7833C (E, C, B, D explicit, plus the one at the top of the
   block) -- the whole function, whose head block 0x3E9C0 blits the hovered-row rect and whose row loop
   (0x3EBA8..0x3EC90) packs two RGB565 triples per row through `Gfx_PackRgb565Pair` into a palette LUT that feeds
   `SoftSurf_BlitRectProxy` / `SoftSurf_BlitSrcRect`. The port's copy of the same function is
   `UiPanel_DrawText` (src/pufo/ui/pufo_panel.c:311..372) and its own comment says the colour step "is still omitted
   here -- the deviation this function records -- so only the string is drawn". So in every frame whose panels go through
   this path the original paints a per-row band and a hovered-row highlight, and the port paints text only. That is the
   strongest explanation for the empty/flat panel areas in the econ, transfer and tact HUD frames, and it is an
   unclosed deviation, not a picture to re-reference. Consequence for the box: the 114 x 17 rect in
   `econ_buy_craft_queued` cannot be the panel highlight, because the port never blits one -- it has to be attributed to
   another blit before that frame is judged.
4. `econ_hangar_combat` -- does the original's hangar arm put controls in the bottom strip in this state?
5. `tact_hud_*` and `tact_briefing*` / `debrief_banner` -- thin bands at rows 1..16 and 311..319: compare against the
   tact HUD draw arms' pad rects before judging.
6. `tact_hud_minimap` rows 16..63 -- a 48-row hole in a live-looking frame, the biggest remaining one.
## Part 157 spec (approved by the user on 2026-10-23): finish `UiPanelMsg_Draw` in the port

Read from the IDB (`UiPanelMsg_Draw @ 0x3e94c`, 1016 B). Layout facts confirmed there: `apLines[row]` = panel + 4*row +
0xBE4, `anLineAux[row]` = panel + 4*row + 0x13B4, `aLineMeta[row][0..5]` = panel + 6*row + 0x10 (stride 6, two RGB
triples). The function does four things the port's `UiPanel_DrawText` (src/pufo/ui/pufo_panel.c:330) does not:

1. `if (bRowRules)` -- `SoftSurf_BlitRectProxy(nRectX, nPosY - 1, nViewW, 1, aStyleRgb[0], aStyleRgb[1], aStyleRgb[2])`:
   the top rule, before anything else.
2. `if (bSelLineHighlight && bHoverActive)` -- `SoftSurf_BlitSrcRect(nRectX + 1, nHoverRow * nFont + nPosY, nViewW - 2,
   nFont, aStyleRgb[3], aStyleRgb[4], aStyleRgb[5], key bColorKey)`: the hovered-row fill, drawn BEFORE the text so the
   text lands on top of it.
3. Per row of the loop (the loop runs from `nUnk3028` to `nLineCount`; only rows with `row - nUnk3028 < nStyle` draw):
   `Gfx_PackRgb565Pair(aStyleRgb[6], aStyleRgb[7], aStyleRgb[8], aStyleRgb[9], aStyleRgb[10], aStyleRgb[11])` first, on
   every iteration.
4. Inside the drawn-row branch: `if (bRowRules)` -- `SoftSurf_BlitRectProxy(nRectX, (visible+1) * nFont + nPosY, nViewW,
   1, aStyleRgb[0..2])`: the rule under that row; then `if (aLineMeta[row][0] || [1] || [2])` --
   `Gfx_PackRgb565Pair(aLineMeta[row][0..2], aLineMeta[row][3..5])`: the row's own colour pair, which is what the text is
   then drawn with.

Only after those comes `Text_MeasureWidth` (by `nUnk7076`: 1 -> half width, 2 -> full width) and `Text_DrawString(nPosX -
measured, (visible) * nFont + (nFont - 16) / 2 + nPosY, psz)`.

Done check for Part 157: (a) `/W4 /WX` clean; (b) a frame check that fails when the hovered-row fill or a row rule is
dropped -- mutation-proved, not asserted; (c) the reference pictures that move are looked at one by one and each change
explained against 0x3e94c before re-referencing; (d) `tools/check.sh` prints the six-gate sentinel.

## Open items

1. The 28 tact `DumpFrame` sites still have no recorded draw call, so their classification is not trustworthy yet.
