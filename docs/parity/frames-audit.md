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
| geoscape_descent_panel | 1..59, 156..235 | CLOSED as a fixture property, not a game property: the frame runs `ClearSurface` + `Geoscape_DrawStatusPanel` only (test_geoscapeframe.c ~1362..1375), so the 140 all-black rows are the harness clearing the surface. It is kept as an arm-isolation diagnostic and must never be read as shipped output; the live sibling `geoscape_descent_offer` paints the same screen with the map under it (0 all-black rows) |
| tact_hud_minimap | 0, 16..63, 317..319 | CLOSED in Part 158: the arm fills 240x320 with index 0 itself (0x1ED54), and the cell field is 4*(cell - scroll) -- measured x 12..239, y 64..303, i.e. scroll (-3, -16) inside the clamp window [-30..570] x [-40..560] for a 60x60 map |
| tact_mission_urban | 1..16 | CLOSED in Part 159: the iso arm's overlay chain is a CMP chain on Map_nUiScreen (3,5,7,0xA,0xB,0xC,0xF) and this frame is screen 0, so only MapView_DrawIsoLayer runs; the tile's screen position is camera-free (0x1B1AC nDrawX = 32*tileX-16, 0x1B1C4 nDrawY = 8*(tileY-3*layer-4), culled at 0x1B1DC when nDrawY+40 <= 0), and the measured top edge is one tile diamond starting at the grid node x = 32*6-16 = 176, y = 16 -- rows 0..16 are outside the map's edge, painted by the layer-0 blank-glyph branch (0x21ED8 gate, the Font_DrawGlyph at 0x21EF4 / 0x21F8C) |
| tact_briefing, tact_briefing_urban, tact_mission_from_descent | 0..7, 311..319 | CLOSED in Part 158: the arm is two calls -- PadImage_Blit of the cydonia pad at (0,0) then UiPanelMsg_Draw (0x1A7A0) -- and the indexed blit writes every pixel including index 0 (0xA3278 `*v24++ = *(pPal + 4*index)`, no skip), so the bands are the pad's own index-0 rows |
| debrief_banner | 1..12, 25..42, 44 | CLOSED in Part 159: ENDMISS.PAD's all-index-0 rows are 0..41 and the arm's centred banner text paints the only lit rows in that range (13..24); rows 42/44 are pad rows whose indices carry black endmiss entries -- rows 12,13,24,25,41,42,43,44,45 checked pixel by pixel against pad index + slot 11 |
| tact_hud_promotion | 1..12, 25..32 | open |
| tact_hud_inventory, 13_tact_unit_panel | 153..159, 199 | CLOSED in Part 159 for the .ppm: INVV.PAD's all-index-0 rows are 139..159 and 199, and pad rows 139..152 are overdrawn by the burden label at (113,140) and the number at (185,140); invv's mode 1 = menu.pal in slot 1, and 95.0% of the frame's pixels are menu.pal colours |
| tact_hud_markers | 319 | CLOSED in Part 159: MSCANV.PAD has no all-index-0 row, and its last row is entirely index 254, which SCANNER.PAL[254] = (0,0,0) renders black (mscanv's mode 16 = scanner.pal); 99.7% of the frame is scanner.pal colours |
| econ_buy_craft_queued, econ_hangar_cap_full, transfer_open, transfer_staged, transfer_committed | 1..2, 29..30, 57..58, 85..87 | CLOSED in Part 158: the four runs are the gaps between the transfer picker's 26-tall base boxes, drawn at step 28 (0x987D8 `3, nY+3, 205, 26`, then 0x9880C and 0x9883C), under the erase rect 0x98738 whose height is `28 * n + 4` = 88 rows with n = 3. Re-measured on the current references: the transfer trio keeps exactly those runs (0..2, 29..30, 57..58, 85..87) and the two econ pictures have none at all after their frames were re-recorded |
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
   CLOSED by Part 157 and Part 158. (i) the IDB draws the whole `UiPanelMsg_Draw` at those call sites and the port now does the same -- `UiPanel_DrawText` (src/pufo/ui/pufo_panel.c:348) holds the head blit, the per-row colour pack, both rules and the per-row meta pack; (ii) the box itself was never a panel highlight: those two dumps had no frame draw behind them at all, and the rect is the transfer picker's control pad with its caption blitted away (the stale-surface section below).
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
   CLOSED: Part 157 put all five steps of 0x3e94c into `UiPanel_DrawText` (committed 67ee0d9, mutation-proved), and Part 158 showed the 114 x 17 rect is the transfer picker's control pad on a stale surface, not a panel fill -- the two pictures now draw their own frame and show the buy screen and the shortfall panel.
4. `econ_hangar_combat` -- CLOSED, see the last section of this file. Short answer: no. The arm's bottom-strip block 0x93298..0x9337C (TEXTS 97 "Экипаж" at y 201, TEXTS 98 "Оборуд." at y 241, both centred on x 53, plus the control pair at block+0x38 and block+0x3C) is gated on `Ship.bCanDogfight @ +0x21` -- 0x93298 `LDRB [R3,#0x21]`, 0x932AC `BEQ loc_93380` -- and the craft on this screen is a type-3 Interceptor, whose byte is 0 by 0x45E28.
5. CLOSED in Part 159, with the pad rects and the pad palettes measured: the bands on `tact_briefing*`, `debrief_banner`, `tact_hud_inventory` and `tact_hud_markers` are the backdrop pads' own index-0 rows (or, for MSCANV.PAD's last row, its index-254 row whose palette entry is black). `tact_mission_urban` 1..16 is the one that is not a pad band.
6. CLOSED in Part 158: `MapView_DrawMinimap @ 0x1ED4C` fills the whole 240x320 with index 0 itself before drawing any cell.
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
2. CLOSED: open questions 5 and 6. Every tact band the sweep table lists is now explained -- the minimap hole (Part 158), the briefing trio, `debrief_banner`, `tact_hud_inventory`, `tact_hud_markers` (Part 159) and `tact_mission_urban` (Part 159 cont.).
3. Open question 1 keeps one half open: the selector's four identical rows are closed (the Part 158 ufopaedia section), but what the pad blitted at 0x9B600 holds in that state is not yet read.
4. CLOSED for the pass itself (Part 159 cont. 2): all 103 references were looked at -- the 93 `tests/ref/*.ppm` in eight labelled montages and the 10 `tests/ref/e2e/*.png` in a ninth -- with a structural screen and a palette-provenance measure behind every one. No layering artifact, no half-painted panel, no unexplained empty area found. What stays open in this file is item 1 (the 28 tact `DumpFrame` sites with no recorded draw call) and the second half of item 3 (what the pad blitted at 0x9B600 holds in the ufopaedia state).
5. CLOSED in Part 159: all four read against their arm. `debrief_banner` = ENDMISS.PAD's index-0 rows 0..41 plus the banner text; `tact_hud_inventory` = INVV.PAD's 139..159 / 199 with the burden labels over 139..152; `tact_hud_markers` = MSCANV.PAD's all-index-254 last row (SCANNER.PAL[254] = (0,0,0)); `tact_mission_urban` = the iso layer's own edge (camera-free tile geometry, no overlay arm on screen 0). `tact_hud_promotion`'s 54 / 71 / 87 were already panel rules from the Part 157 review.

## Part 157 review: the 29 references that moved, read against 0x3e94c (2026-10-23)

Method. Each changed reference was diffed against HEAD -- the changed-pixel mask, the old->new colour pairs and the
changed row bands -- converted to a [current | changed-only] sheet in build/crops/view157/s_<name>.png, and the sheet
was looked at. The 29 pictures give 23 distinct (mask, pairs) signatures; the twins share both, so each signature was
reviewed once and its twins listed: (econ_buy_craft_queued, econ_hangar_cap_full, transfer_staged, transfer_committed),
(econ_manuf_alloc, econ_manuf_done), (geoscape_research, geoscape_research_apply), (tact_briefing_urban,
tact_mission_from_descent).

Every change is one of the two UiPanelMsg_Draw steps or the colour consequence of its packs. Nothing else appeared.

1. The rule blits 0x3E9BC (nPosY - 1) and 0x3EB30 ((nVisible + 1) * nFont + nPosY). Their colour is the panel's
   aStyleRgb[0..2]: grey 0xB4 where the builder writes the triple (buy/sell 0x77218, the plane store panels,
   ufopaedia 0x9A9BC and 0x9AE98, the hangar panels), and index 0 -- black -- where it does not. The five tact panels
   get only SetBytes6to11 (pufo_tactinit.c layout table): panels 0, 1 and 4 clear bRowRules, panels 2 and 3 set it, so
   tact_hud_promotion shows black rules at y 54, 71, 87 (nPosY 55, nFont 16) and tact_briefing* shows none at all.
2. Panel rows, and any arm text drawn after a panel, now take the panel's own style pair: the pack at 0x3EAB8 writes
   palette slot 2 entries 33/27 and Text_DrawString @ 0x2d798 reads them. Seen in geoscape_hangar's caption,
   geoscape_options_base's title and column headers, geoscape_research's column captions, ufopaedia_entry's body,
   tact_briefing's paragraph, the BaseInfo caption (0x57284, which follows the three panels at 0x57218..0x57230).
3. No hovered-row fill appears in any of the 29. In every one of these states bHoverActive is clear at the dump, and
   the fill's gate at 0x3E9E4 keeps it off.

Row periodicity is what makes each rule reviewable without judgement calls: econ_buysell_* / econ_research_* /
geoscape_research_* change rows 69, 86, 102, 118, 134, 150 (nPosY 70, nFont 16) with the glyph bands between them;
ufopaedia_index changes 49, 74, 98, 122, 146, 170, 194, 218 (the selector panel's nPosY 50 with nFont 24, 0x9A93C);
geoscape_multi_box 103, 120, 136 (the status panel, nFont 16); geoscape_hangar 109, 126, 142, 158 (nPosY 110);
tact_hud_promotion 54, 71, 87. In econ_buy_craft_queued the rules at 69 and 86 are absent because the destination
overlay's erase rects (0x98738 and 0x9877C, height 28 * nBaseCount + 4 = 88) cover y 0..87 -- the first rule that
survives there is 102, and 102 is exactly what the picture changed at.

The box (item 2 of this ledger), measured again after the change: x 61..178, y 94..110, a 1 px frame in 0x009DAC
around a 0x00555A interior, byte-identical to HEAD in all four pictures that show it. It is not a UiPanelMsg_Draw
fill: a fill is (nViewW - 2) x nFont at nRectX + 1, and no panel of this screen produces 118 x 17 (A 123 x 16, B and C
13 x 16, D 228 x 16, E 43 x 16). Panel E's fill would land at x 191..234, y 70..85 -- that region is byte-identical to
HEAD, so no fill was painted there either. Item 2 stays open with the same next check: which blit draws this frame, and
why transfer_open shows a caption inside the same frame while the queued states do not.

Pre-existing, measured, not Part 157: the manuf and research overlays paint their own rows over the panel rows behind
them (econ_manuf_alloc, geoscape_research_allocated -- both show the overlap in HEAD as well). New open item 157a: read
the two overlay arms in the IDB and check whether they blit an opaque pad before their text, which the port would then
be missing.

Test behind the change: TestPanelMsgDrawPasses in tests/test_panel.c (20 checks), and 13 mutations of
src/pufo/ui/pufo_panel.c that each broke the checks they had to break -- the full table is in game-core.md, Part 157.

## Part 157 review, the e2e picture that moved: 02b_base_screen (2026-10-23)

`tools/check.sh` moved one reference outside the frame gate: tests/ref/e2e/02b_base_screen.png, 11255 px, the base
information window. Reviewed the same way (mask, colour pairs, row bands, sheet s_e2e_02b_base_screen.png).

The change is colour only -- no blit, no geometry, and the picture gains no ink anywhere. Two pairs, in row bands that
are exactly the names panel's rows (nPosY 40, nFont 16, y = row * 16 + 40):

  rows 0, 5, 12  (bands 43..54, 123..132, 235..246)  41FFFF -> C5858B and 004852 -> 310808   x2926
  other drawn rows (1..3, 6..10, 13..15)             41FFFF -> EEEEF6 and 004852 -> 101410   x8329

Those three rows are the three the ARM marks with a row meta pair: 0x55A6C, 0x55A90, 0x55AB4 and 0x55AD8 are four
`BL UiPanel_SetLineMeta` calls, each reached by `LDR R0, [R5]` -- the names panel only -- with nLine 0, 5, 0xC, 0x10.
The fourth meta row, 0x10 = 16, is packed and not drawn, and the picture has no changed band at 296..311: nStyle is 16,
so rows 0..15 are drawn and 16 stops at the 0x3EACC test. The two blank separator rows 4 and 11 (TEXTS slot -1 in
g_anBaseInfoNameRec) change nothing at all, which is what the empty-string skip does.

The colours decode to the packs the panels own. The value rows' second colour 0x101410 unpacks to r5 2, g6 5, b5 1 =
4258 = 0x10A2, exactly the second triple Gfx_PackRgb565Pair packs from aStyleRgb[9..11] = 0x14,0x14,0x14. The three
header rows' second colour 0x310808 unpacks to r5 6, g6 2, b5 1 = 12353 = 0x3041, exactly the meta row's second triple
(50, 10, 10) -> (6, 2, 1). In HEAD both kinds of row carried one stale pair (41FFFF / 004852) because the port's
UiPanel_DrawText packed nothing at all -- the deviation this Part closes.

No rule and no fill appear here, as they should not: BaseInfoPanelCommon clears bRowRules and bSelLineHighlight, and no
changed band is a full-width single row (the widest changed row is 118 px). The "Азия-1" caption at 16..25 changed with
the pair the panels leave behind, the 0x57284-after-0x57230 order recorded in game-core.md.

## Part 158: the framed empty box is a stale surface, not a missing blit (2026-10-23)

Item 2 of this ledger is closed, and it was never a draw defect.

Method. The dump helper only writes the surface -- it does not draw. So each `DumpFrame` was instrumented to print a
surface hash, and the two frame entry points (`World_DrawGeoscapeFrame`, `World_TactFrame`) to print a counter. Both
probes were temporary, restored from saved copies, and verified: md5 of src/pufo/app/pufo_world.c and
tests/test_geoscapeframe.c equal their saved copies, `grep -rn MUTATION src tests` empty. One run of
test_geoscapeframe, 101 dumps, 611 frame draws.

Result. Exactly two dumps had no frame draw since the previous dump: econ_buy_craft_queued and econ_hangar_cap_full.
Their pictures were the transfer picker's frame, painted before the buy screen even opened. The one region where they
differ from the transfer trio is rows 97..106, x 61..178 -- 415 px -- which is the picker's confirm caption: the ARM
packs it at 0x98904 (0x44,0xF4,0xFC / 4,0x46,0x4B) and draws TEXTS[1267] at 0x98968, `y = 28 * nBaseCount + 10` with
nBaseCount read signed at block+0x618 (0x98944), x = 120 - w/2 (0x98954..0x98960). A press after that frame re-blits
the confirm pad through UiControl_HitTest (0x1A42C on a hit, the miss path that releases a held control), and the pad
is bare where the caption was. Nothing re-drew the frame, so the bare pad is what those two references recorded. The
framed area is the control pad (118 x 17 at x 61, y 93), not a panel fill: no panel of that screen gives
(nViewW - 2) x nFont = 118 x 17, and the fill region of panel E (x 191..234, y 70..85) is byte-identical to HEAD.

Fix, in the fixture: the two dumps now ClearSurface + DrawFrame first, like every other dump in this file. The two
references were re-recorded and reviewed.

econ_buy_craft_queued is now the buy screen: the funds line "1446930 $" and the total "500000 $" (the fixture's own
printed pair), the item list with the queue column holding the single 1 on the craft row (the 0x75D84 pair), the "<"
">" columns, the close control and the OK pad. econ_hangar_cap_full is now the refusal: the shortfall panel with
"Нет свободного ангара" -- the expand frame at 20,130 with Ui_AnimExpandBlit's own rects (0x95E18 outer 20,130,204,6 /
0x95E6C inner 22,132,200,2 / 0x95EA0 + 0x95ECC at y 200 and 202 / the side bands 0x95F00..0x95F80), measured as outer
rows 130..131 and 204..205 x 20..223 and inner rows 132..133 and 202..203 x 22..221; the centred line at (120, 158)
inside 0x789C0..0x78C24 drawn in the 0x789B4 pair (220,80,80 / 50,30,30), measured as ink columns 51..188 (centred on
120) and glyph rows 163..170. The box interior is not filled by the ARM -- Ui_AnimExpandBlit draws eight band rects
and no fill -- so the item rows showing through it is shipped behaviour, not a hole.

The other four byte-identical dumps this sweep found are all faithful, each with an IDB reason:

  transfer_staged / transfer_committed = transfer_open -- the staged digit and the commit's row changes all sit on
  panel rows 70..85, under the overlay's black erase band (0x9877C, y 0..87). Recorded in the fixture already.
  tact_hud_load_slots = tact_hud_save_slots -- TactHud_DrawModalPanel draws its record only when the modal mode is
  exactly 1 (`nModalMode != 1` returns before UiModal_DrawDialog), and Menu_OpenSaveSlots (0x188A4) and
  Menu_OpenLoadSlots (0x18864) both leave the list mode at 0. Records 1103 and 1102 are never drawn, so the two
  screens are the same eight pads and the same seven labels.
  econ_research_done = econ_research_running -- the finish (0x743A4..0x74798: bUnlocked 1 at 0x7440C, the allocation
  columns cleared at 0x7442C..0x74444, the line appended with sound at 0x74554) changes no drawn value: the topic,
  quantity and progress panels are rebuilt only by RebuildTopicList on a press, and the detail block is skipped by
  the 0x73D64 gate (no row selected), which is why neither frame carries the "Ученых доступно" labels at 0x73DE8..
  0x73E64 or the three numbers at 0x73F24..0x74068. The completion is real in state and invisible on this screen
  until the next press -- recorded, not fixed.
  econ_craft_loaded = econ_craft_spawned -- both frames are screen 1 (the base view the hangar closes to, 0x8C094),
  and the base view draws none of the cargo and shelf columns the load round moved (0x78E34 / 0x78E4C).

IDB: 0x98968's item comment now carries both the Text_DrawString glyph-base note and the caption-vs-pad-blit note
(690 characters, read back through the listing), and the IDB was saved.

## Part 158: 157a closed -- the overlay overlap is the original's own coordinates (2026-10-23)

The manuf and research allocation overlays draw their text over the panel rows behind them (econ_manuf_alloc,
geoscape_research_allocated). Checked whether the original erases the area first. It does not.

Enumerated every surface blit in each ARM function. ManufScreen_DrawFrame (0x6F9A4): 11 SoftSurf_BlitRectProxy calls --
eight rails at 0x6F9F4, 0x6FA24, 0x6FA48, 0x6FA6C, 0x6FAC4, 0x6FAEC, 0x6FB1C, 0x6FB44 and three separators at
0x6FD68, 0x6FD8C, 0x6FDB0. ResearchScreen_Draw (0x73A34, 2108 B): 10 -- eight rails at 0x73A84..0x73BD0 and two
separators at 0x73D30, 0x73D54. Neither function calls SoftSurf_BlitSrcRect, blits a pad into the overlay area, or
fills a rectangle there. The port's blit set is the same count in both.

The overlay frame itself is Ui_AnimExpandBlit, which paints eight band rects and no interior (0x95E18 outer top,
0x95E6C inner top, 0x95EA0/0x95ECC bottom, 0x95F00/0x95F28/0x95F58/0x95F80 sides) -- so whatever stands behind the
frame stays visible, and it does in the pictures.

The collision is in the original's own coordinates. The research detail block draws its labels at fixed y: 0x73DE8 at
(10, 118), 0x73E28 at (10, 132), 0x73E64 at (10, 146), 0x73EC8 at (40, 164), 0x73F04 at (40, 184), and its three
numbers at x 195 through 0x73F24..0x74068. The topic panel puts its rows at nPosY 70 with nFont 16, i.e. y 70, 86, 102,
118, 134 -- rows 3 and 4 land exactly on the "Ученых доступно" and "Свободн. мест в лаборатории" labels, which is what
geoscape_research_allocated shows. Same shape on the manuf screen with its own panel band. Recorded as shipped layout,
not a port omission.

## Part 158: ufopaedia_index closed -- the four identical rows are four text records (2026-10-23)

The index picture shows the selector panel's nine rows: four carry "не исп." and five are blank, over the dashed frame
with the close and the two arrow controls. That is the shipped content, already pinned at 0x9AF2C..0x9B0BC and
re-checked here against the loaded table: the ARM labels the nine selector rows with the nine sources
0x105EE6..0x10654E, 205 apart, i.e. records 1646..1654; lang.dat TEXTS.DAT loads 1650 records, and records 1646, 1647,
1648 and 1649 are each the same string "не исп." -- "не исп.", the label for a ufopaedia
category that has no entries in the shipped data. Records 1650..1654 fall past the loaded range, so those five rows copy
the zero slack and draw nothing. Four identical rows and five blank ones is therefore what the original paints, not a
list the port failed to build. The category panels are built separately (pass 1 0x9AB04..0x9AF14 counts the shown
entries, pass 2 0x9B0C4..0x9B4FC writes the names into packed rows), and the entry screens ufopaedia_entry /
ufopaedia_entry_next carry the names.

## Part 158: econ_hangar_combat closed -- the empty bottom strip is the dogfight gate (2026-10-23)

The question was whether the hangar arm puts controls in the bottom strip of the craft-detail view. Reading the arm in
the IDB (`Geoscape_Draw @ 0x8C180`, arm block 0x9309C..0x93438; the port's copy is `Geoscape_DrawPlaneArm` at
src/pufo/game/pufo_geoscape.c:6123), in draw order:

  * 0x93118..0x93148 -- two controls, only with the store columns committed. Not this state (no store round ran on this
    craft).
  * 0x9314C..0x93180 -- two controls, only with the list panels built. Not this state.
  * 0x93198..0x93290 -- one crew control per crew slot, gated on all four flags being clear (`bStatPanelAOpen 0x161F18`,
    `bStatPanelBOpen 0x161F1C`, `bPlaneListPanelsBuilt`, `bPlaneStoresCommitted`), with the digit "1" at (28, 103) from
    0x93234 and "2" at (205, 103) from 0x9325C. This is the block that paints the picture's two pads and their digits.
  * 0x93298..0x9337C -- the caption block: `LDRB [R3,#0x21]` = Ship.bCanDogfight, `BEQ loc_93380` when clear; otherwise
    the loop 0x932C8..0x93378 (counter 2 to 4) blits the block's last two controls (+0x38, +0x3C) and draws TEXTS 97
    "Экипаж" at y 201 (0x93330) and TEXTS 98 "Оборуд." at y 241 (0x9335C), both centred on x 53.
  * 0x93380 → 0x91DA8 → 0x93058 -- the close control, seated at (224, 1) by the hangar Init at 0x7DDB4. It is the only
    control every hangar frame draws, and it is the blue "x" at the top right of the picture.

The claim that the hangar views "omit the controls 0x91DA8 / 0x91E08 add" is not what the IDB says: 0x91DA8 is three
instructions (load `*g_pUiCloseCtl @ 0x186B00`, branch to the shared blit at 0x93058), and 0x91E08 sits inside the
screen-1 placement arm that starts at 0x91DC8 -- pad `pBaseFacScreenPad @ 0x186BA0`, three side rails at x 2 / 237 / 210,
the ten nav controls and the ten action captions. The hangar arm reaches 0x91DA8 and nothing after it.

Why the caption block does not run here: the craft is type 3. `Ship_ctor @ 0x45C20` writes Ship.bCanDogfight @ +0x21 at
exactly four sites and nowhere else -- 1 at 0x45DA4 (type 0), 1 at 0x45E84 (types 1 and 2 share the block 0x45E7C..0x45E88),
0 at 0x45E28 (type 3), 0 at 0x45E18 (type 4). Ship.bCrewSlots @ +0x14 is written 2 at 0x45D88, 1 for type 1 at 0x45D8C,
0 for type 0 at 0x45D9C -- which is why a 2-slot Interceptor draws both digits.

Measured in the reference itself (build/crops/hangcomb.ppm, converted from tests/ref/econ_hangar_combat.png; the first
attempt at this measurement was wrong and is recorded: the PPM header parser ate one pixel byte and shifted every read by
one byte, so the first pass found nothing anywhere):

  * The title's ink breaks into 11 clusters: five glyphs, a 3-wide bar occupying row 19 only at x 121..123, three glyphs,
    a second 3-wide bar at x 146..148, then one digit. That is TEXTS 418 read as "Перех-тик" (CF E5 F0 E5 F5 2D F7 E8 EA),
    then g_abCraftNameSep[3] = '-', then bType 1 -- the name Ship_FormatDisplayName @ 0x666CC gives a type-3 craft. The
    name is the type proof: the two dashes are the proof, not a guess.
  * The crew digits sit at x 30..32 rows 107..114 and x 206..212 rows 107..114, i.e. the ARM's (28, 103) and (205, 103)
    with the font's own glyph offsets. Their ink is (90, 255, 255), from the pack 0x931F8..0x9321C (68, 244, 252 /
    4, 70, 75).
  * That caption block packs the same pair (0x932EC..0x93310), so a caption would be the same colour. Scanning every
    pixel below row 120 for it: 0 matches. Rows 199..205 and 239..245 hold only the backdrop pad's own browns and greys
    ((164,149,131), (180,161,139) / (74,80,82), (180,153,123)) -- nothing drawn, nothing erased.

So the empty lower half of econ_hangar_combat is the shipped dogfight gate sitting over the backdrop pad (0x930A0), not a
port omission. IDB: `Ship_ctor`'s comment now carries the four +0x21 sites, the three +0x14 sites, the type dispatch
table, and a correction of its old claim that no code indexes a text record by bCraftType -- Ship_FormatDisplayName does
(903 characters, read back identical, IDB saved).

## Part 158: the tact black bands -- minimap closed, the briefing trio closed (2026-10-23)

Baseline first: the sweep was re-run over the current corpus (93 `tests/ref/*.ppm`; the `tests/ref/e2e/` set is PNG-only)
and 13 pictures have an all-black run now, against 21 in the original table. The difference is the menu family --
`00_mainmenu`, `10_menu_at_boot` and `11_slot_list` no longer have their 264..268 / 313..319 runs, and `econ_buy_craft_queued`
and `econ_hangar_cap_full` have none at all. Those runs disappeared when Part 157 gave the panels their hovered-row fill
and Part 158 gave those two dumps a frame, which is the expected direction for a fix.

`tact_hud_minimap` (rows 16..63 black, the biggest remaining band) is closed as shipped behaviour of its own arm.
Screen 2 goes to `MapView_DrawMinimap @ 0x1ED4C` (Render_Frame case 2 at 0x224B8, which REPLACES the iso layer). That
function's first instruction after the prologue is `SoftSurf_BlitRectProxy(x 0, y 0, w 0xF0 = 240, h 0x140 = 320,
colours 0, 0, 0)` -- the arm paints the whole screen black itself. Then the clamp (max `40*(nMapDimX-3)/4` then min -30,
max `40*(nMapDimY-4)/4` then min -40, each comparing against what the previous clamp stored -- matches the port), then
the two cell passes draw `Font_DrawGlyph(4*(tileX - nScrollX), 4*(tileY - nScrollY), ...)`.

Measured in tests/ref/tact_hud_minimap.ppm: the ground pass covers x 12..239, y 64..303, completely filled
(52941 of 228 x 240 px in the two ground colours (164,97,32) and (148,80,32), the rest overdrawn). 228 x 240 at 4 px per
cell is 57 x 60 cells for a 60 x 60 map clipped at the right edge, and the origin says `nScrollX = -3`, `nScrollY = -16`
-- inside the clamp window (max 570 / 560 for a 60x60 map, min -30 / -40). So the 48-row band above the field and the
16 rows below it are the arm's own black field where the cell passes do not reach, at a scroll value the original's own
clamp allows. The inset in the middle of the field (the pink/blue grid with the green item marks) is the second pass:
the mission's floor/wall/ceiling glyphs over the ground glyphs.

`tact_briefing`, `tact_briefing_urban` and `tact_mission_from_descent` (rows 0..7 and 311..319 black, identical ink
bounds in all three -- first lit row 8, last lit row 310, a full-width lit row somewhere in the middle) are closed too.
Their arm is Render_Frame case 0x14 at 0x223DC, and the whole handler at 0x1A7A0 is two calls: `PadImage_Blit` of the
pad pointer at unk_15692C, then `UiPanelMsg_Draw` of the panel at unk_1571EC. The port's `Cydonia_BlitThenDrawPanel`
(src/pufo/ui/pufo_panel.c:553) is those two calls. `PadImage_Blit @ 0x110DC` is a single call to
`SoftSurf_BlitIndexed(x, y, w, h, pGlyph, nPal = [pad+0x10])`, and the inner loop at 0xA3264..0xA3294 is

    *v24++ = *(_DWORD *)(pPal + 4 * *(unsigned __int8 *)v18);

-- every index is written, index 0 included, with no key skip (the key form is the separate `PadImage_BlitEx @ 0x11110`).
So a pad row whose pixels are index 0 paints palette entry 0, and the bands are the cydonia pad's own art. The fixture
check at that dump already proves nothing else is drawn on this path: the frame paint against the pad alone is the one
centred panel line (`nBelowInk == 0`, `nOutsideInk == 0`), which is what "the arm REPLACES the iso layer" means.

For the record, the same mechanism explains the row-rule runs in `tact_hud_promotion` (54 / 71 / 87): a panel rule is
`SoftSurf_BlitRectProxy(..., aStyleRgb[0], aStyleRgb[1], aStyleRgb[2])`, and a panel whose style triple is 0,0,0 paints
a black rule row every `nFont` rows. That was measured in the Part 157 review; it is recorded here only to keep the
sweep table consistent.

## Part 158: the four remaining tact bands, measured but not yet adjudicated (2026-10-23)

Each picture was read as a per-row coverage profile (rows with all 240 px non-black, rows partially covered and their
x span, rows fully black). This is the state of these four:

  * `tact_hud_markers` -- rows 0..45 and 261..318 fully covered, 46..260 partially covered but spanning x 0..239,
    only row 319 black. The arm is `TactHud_DrawUnitMarkers @ 0x20C58` (Render_Frame case 6, the call at 0x224D0, which
    replaces the iso layer). Next check: the marker pad's own rect and art -- whether the pad ends at 318 or its last
    row is index 0 (index 0 is painted, not skipped: 0xA3278), and whether the two unit-marker loops could reach row 319.
  * `tact_hud_inventory` -- rows 0..21 and 200..319 fully covered, 22..152 and 160..198 partially covered with the ink
    confined to x 22..227, black at 153..159 (7 rows) and 199 (1 row). The arm is `TactHud_DrawInventoryScreen @ 0x1F58C`
    (case 1, call at 0x224AC): `PadImage_Blit(pInvPad)`, the control pads, `InventoryHighlight` when an item is held with
    forcenodrag set, the burden label at (113,140) and the burden number at (185,140), the header at (3,2),
    `TactHud_DrawItemStrip`, the portrait `PadImage_BlitEx(..., 0)`, and while an item is held the name at (3,203) and the
    dragged glyph. So the drawing region narrower than the screen (x 22..227) is the inventory pad's own rect, and the two
    black runs sit inside it. Next check: the pad's rect and its index-0 rows against 153..159 and 199, and the strip's
    top edge against 200.
  * `tact_mission_urban` -- rows 0..16 black, ink from row 17 (a 4-to-10-px wide cluster at x 168..177, i.e. a control
    pad), rows 239..319 fully covered, 83..94 and 104..192 fully covered. This is the iso group, so the map content comes
    from `MapView_DrawIsoLayer @ 0x22414`. The Part 156 hypothesis (a fixture scroll outside an enforced clamp) is
    ruled out for this one: the iso arm has no clamp at all -- it computes `Map_IsoOffsetA/B(dim, nCamScrollX,
    nCamScrollY)` and walks tiles `0..nTileCountY/X` with no bound check on the camera pair, and searching the whole
    binary for the clamp constant -30 (`MOV R2,#0xFFFFFFE2`) finds it only in `MapView_DrawMinimap` at 0x1EE0C (the
    screen-2 arm) and twice in `Tact_CheckBattleEnd` (0x354AC, 0x3562C, unrelated). So the 17 empty top rows are the iso
    offset pair's own result. Next check: the map ink's bounding box against `4*` the offset this state's camera pair
    produces.
  * `debrief_banner` -- rows 0..12, 25..42 and 44 black; ink from row 13 (a narrow cluster at x 99..120 -- the centred
    banner text `Text_DrawString(120 - w/2, 10, ...)` from `TactHud_DrawStatusBanner @ 0x1E920`), a wide partial band
    45..289, and full coverage only at 290..291 and 297..300. Next check: the banner backdrop pad's rect and index-0 rows
    against 0..12 / 25..42 / 44, plus the two panel draws' rows against the 45..289 band.

None of these four is claimed fixed. The measurements above are what the next pass starts from.

## Part 159: the slot-33 census, and every remaining tact band closed by its own pad and palette (2026-10-23)

**The open slot-33 question was a gap in the record, not in the port.** Part 158 left unresolved which palette holds
slot 33, the mode the Cydonia briefing pad is loaded with (`Tact_InitMission`, `MOV R1, #0x21` at 0x3D2D4). Census over
every `Palette_Load @ 0x3E47C` call site: 51 code sites, each passing a literal value in R1, no computed slot anywhere,
slots 0..34. Slot 33 has exactly one writer -- `Geoscape_InitUI 0x860BC..0x860C4`, `LDR R0, =aCydoniaPal "cydonia.pal";
MOV R1, #0x21; BL Palette_Load`. The port's own table has the same pair (`src/pufo/ui/pufo_geoiinit.c:138`
`{ "cydonia.pal", 33 }`), so nothing was out of parity; the census is now recorded in the IDB as a function comment on
`PadImage_Blit @ 0x110DC` (the mode-is-the-slot rule plus the whole census, 1033 chars, read back identical) and a line
comment at 0x860BC. `idb_save` ok.

**The claim "the briefing colours match no .PAL entry" was a measurement error, and it is retracted.** That test
compared the picture against the raw `.PAL` triples. The shipped order is: load the palettes, then
`Tact_BuildPaletteLuts @ 0x3734C`, which rewrites all 57 slots in place -- every channel divided by
`0.1 * Map_nGamma + 0.7` and clamped per channel -- and only then draw. Compared the right way (565 pack from the
branch at 0x3E4C0, then the LUT at gamma 0, then the dump's own expansion `(v>>11)&0x1F)*255/31` etc.), the match is
exact for the pad content. The same arithmetic explains the black bands directly: `CYDONIA.PAL[0] = (5,2,4)` packs to 0
(5>>3 = 0, 2>>2 = 0, 4>>3 = 0), and LUT(0) = 0, so CYDONIA.PAD's index-0 rows render exactly black rather than dark red.

**Colour coverage of the whole frame, palette = the pad's own mode slot** (`build/crops/pal158b.py`, `pal158c.py`; the
leftovers are always the arm's own text colours, not pad pixels):

| picture | palette that explains it | px of 76800 | leftover |
|---|---|---|---|
| `tact_briefing` | cydonia.pal, slot 33 | 76714 (99.9%) | 1 colour, 86 px = (238,234,246), the panel line |
| `tact_briefing_urban` | cydonia.pal, slot 33 | 73174 (95.3%) | 1 colour, 3626 px = the same panel text |
| `tact_mission_from_descent` | cydonia.pal, slot 33 | 73174 (95.3%) | the same one colour |
| `tact_hud_inventory` | menu.pal, slot 1 (invv mode 1) | 72946 (95.0%) | 53 colours, 3854 px = item-strip glyphs and labels |
| `tact_hud_markers` | scanner.pal, slot 16 (mscanv mode 16) | 76575 (99.7%) | 10 colours, 225 px = marker and text colours |
| `debrief_banner` | endmiss.pal, slot 11 (endmiss mode 11) | 73381 (95.5%) | 8 colours, 3419 px = banner and control text |
| `tact_mission_urban` | tactical.pal, slot 34 | 56579 (73.7%) | 80 colours -- expected for the iso map, which is not one pad |

Gamma 0 gives the best match for every one of them, which is what `Map_nGamma`'s default produces, and no other
palette in main.dat comes close for these pictures.

**Row-by-row pad art** (`build/crops/padzero158.py`, `build/crops/row158.py`). Each backdrop pad's all-index-0 rows
equal its picture's black rows:

  * `CYDONIA.PAD` 240x320: 0..7 and 311..319 -- identical to `tact_briefing`, `tact_briefing_urban` and
    `tact_mission_from_descent`, no more and no less.
  * `ENDMISS.PAD`: 0..41. `debrief_banner`'s black rows are 0..12, 25..42 and 44, all inside that run plus the two
    rows where the pad's own indices carry black entries (row 42 = 204 index-0 px with 8 px each of index 255 and 189
    and 5 of 254; row 44 = 197 index-0 px with 30 of index 126). The lit rows 13..24 in the middle of the band are the
    arm's centred banner text (`TactHud_DrawStatusBanner @ 0x1E920`, `Text_DrawString(120 - w/2, 10, ...)`) and the
    only colour there is (0,125,0).
  * `INVV.PAD`: 139..159 and 199; the picture's black rows are 153..159 and 199 because pad rows 139..152 are
    overdrawn by the burden label at (113,140) and the burden number at (185,140).
  * `MSCANV.PAD`: no all-index-0 row at all; rows 317 and 318 are entirely index 253 = (0,16,0) and row 319 is
    entirely index 254 = (0,0,0), which is exactly the picture's one black row.

Rows 12, 13, 24, 25, 41, 42, 43, 44, 45 of `debrief_banner` and rows 317, 318, 319 of `tact_hud_markers` were compared
as `pad index -> slot palette colour` against the picture's own pixels and agree row for row, including the rows that
are not pure index 0.

Closed as shipped by this Part: `tact_hud_markers`, `tact_hud_inventory` (the .ppm; `tests/ref/e2e/13_tact_unit_panel`
shares the same arm but is a PNG and was not re-measured here), `debrief_banner`, and the briefing trio with data proof
instead of only the index-0 mechanism.

Still open, and now the only band left in the tact family: `tact_mission_urban` rows 1..16. Useful facts fixed while
getting here, one of them a correction: `MapView_EnterLookMode @ 0x163D4`'s `-30 / -40` pair feeds
`Map_nMinimapScrollX/Y @ 0x118864/0x118868`, NOT the iso origin -- so the clamp window Part 158 pinned belongs to the
minimap arm only. The iso origin is `Map_IsoOffsetA @ 0x26F88` = `camX - 5*dimX + camY` and `Map_IsoOffsetB @ 0x26FA8`
= `5*dimY - camX + camY - 1` (dimX/dimY read as bytes at Map+86299 / Map+86300), and `MapView_SetCamOffset @ 0x276F0`
is what writes the camera pair `unk_1183F8/0x1183F9`: `n = a2 + 2*(a3 + 8)`, `byte0 = n / 32`,
`byte1 = (a2/2 - a3 <= 0 ? 8 : -8 + (a2/2 - a3)) / -16`. The next pass starts from those two formulas and this
fixture's camera values.

## Part 159 (cont.): `tact_mission_urban` closed as the map's own edge (2026-10-23)

What the arm is allowed to draw here, from the IDB. `Render_Frame` reaches the iso group through 0x22414, which is
always `MapView_DrawIsoLayer`; the block after it is a CMP chain on `Map_nUiScreen` -- 3 -> 0x2249C (roster),
5 -> 0x22490 (status text), 7 -> 0x22484 (labeled pads), 0xA -> 0x22478 (modal left), 0xB -> 0x22470 (modal right),
0xC -> 0x22464 (top bar), 0xF -> 0x22458 (action buttons) -- and every other value falls to `def_2237C`. This dump is
the plain battlefield at screen 0 (the fixture's own assertion: "the battlefield is what screen 0 paints"), so no
overlay arm runs, and the modal arm is behind `Modal_bOpen`. Nothing but map tiles is expected in the top rows, and the
dump's colour check agrees: no single palette explains this frame (tactical.pal reaches only 73.7%), because it is a
map and not a pad.

The covered rows are fixed by the arm, not by the camera. `MapView_bResolveIsoCell @ 0x1B0D4`: 0x1B1AC
`nDrawX = 32*nTileX - 16` (or `- 32` when `nTileY` is odd), 0x1B1C4 `nDrawY = 8*(nTileY - 3*nLayer - 4)`, and the cell
is dropped when `bOob` or `nDrawY + 40 <= 0` (0x1B1DC). The camera (`Map_nCamScrollX/Y`) enters only `nMapY` (0x1B0F8)
and `nMapX` (0x1B108, plus the odd-row +1 at 0x1B130) -- it chooses which map cell a tile shows and cannot move a tile
on screen. So no fixture camera can be "outside a clamp" here: there is no clamp in this arm, and the -30 / -40 pair
Part 158 pinned belongs to `MapView_DrawMinimap @ 0x1ED4C` through `Map_nMinimapScrollX/Y @ 0x118864/0x118868`.
Layer 0's tile rows start at `nDrawY = -32, -24, -16, -8, 0, 8, ...`, and a tile whose cell resolves out of range falls
to the layer-0 fallback: 0x21ED8 `CMP R3, #0` gating the `Font_DrawGlyph` at 0x21EF4 (and its twin at 0x21F8C in the
second pass), the port's `Font_DrawGlyph(nDrawX, nDrawY, paGroupBase[0], 0)`.

Measured in `tests/ref/tact_mission_urban.ppm` (`build/crops/isogrid159.py`): the ink starts at row 17 as ONE 4-px run
at x 174..177, and that run grows by exactly 1 px per row -- 172..177 at row 18, 170..177 at 19, ... 144..177 at row 32.
That is a straight edge, not scattered missing furniture, and its starting point sits on the grid: the draw origin of
tile (tileX 6, tileY 6, layer 0) is `nDrawX = 32*6 - 16 = 176`, `nDrawY = 8*(6 - 0 - 4) = 16`, i.e. the measured
first-ink pixel (177, 17) is one pixel inside the glyph drawn at the grid node (176, 16). Every tile above it either is
culled by the 40-px test or maps outside the map and takes the blank-glyph branch, so rows 0..16 black is the area
north of the map's edge, which is what the original's own fallback paints there.

IDB: line comment at 0x1B1C4 with the camera-free geometry and that consequence (731 chars, read back identical),
`idb_save` ok. The tact family now has no unexplained band; the corpus families in Open item 4 are the remaining work.

## Part 159 (cont. 2): every reference picture looked at, 93 .ppm + 10 e2e .png (2026-10-23)

Method, three layers, all reproducible:

  * `build/crops/montage159.py` -> `build/crops/view159/sheet01..08.png`: all 93 `tests/ref/*.ppm` in 4x3 labelled
    montages, and `sheet09_e2e.png` for the 10 `tests/ref/e2e/*.png`. Every picture was looked at.
  * `build/crops/lit159.py`: per picture -- lit fraction, count of all-black rows, count of rows under 5% lit, the four
    dominant colours. This is the screen for "an empty place nobody explained".
  * `build/crops/pal158b.py`: per picture, the shipped `.PAL` member whose 565-packed, gamma-LUT colours explain the most
    pixels (the table is in the first Part 159 section).

Structural result: no reference is half-painted. The pictures with all-black rows are exactly the ones already
adjudicated -- `geoscape_descent_panel` 140 (the isolation fixture, now closed as such), `tact_hud_minimap` 52,
`tact_hud_promotion` 24, `tact_briefing` / `tact_briefing_urban` / `tact_mission_from_descent` / `tact_mission_urban` 17,
`debrief_banner` 32, `transfer_*` 10. Everything else in the corpus has 0 all-black rows; `ufopaedia_index` has none
either (79.3% lit, and its dark look is its own pad's near-black greens (0,4,0) x15665 and (0,8,0) x10104). The geoscape
frames are fully painted -- `geoscape_options_volume`, `geoscape_multi_box` and `geoscape_target_panel` contain zero
pure-black pixels, and `geoscape_research_rows` has 932 scattered black pixels inside its panel text (bbox x 11..234,
y 16..66), i.e. gaps between glyphs, not a band.

Three readings at montage scale were wrong and are retracted, each disproved by measurement:

  * "a dark rectangle at the top-left of the geoscape family" -- there is none: those frames have 0..932 pure-black
    pixels and the dark area is the map's own ocean colours (`build/crops/geoblack159.py`).
  * "the boot-menu title is drawn twice" -- `tests/ref/e2e/10_menu_at_boot` and `11_slot_list` have no self-similarity at
    any large shift (best 57.8% at shift 2 px, which is just smooth gradient); what looked doubled is the same title art
    appearing in two adjacent montage cells.
  * "the main menu's list shows two numeric lines" -- at 2x (`view159/zoom_mainmenu_full.png`) the four items read
    "Старт новой игры", "Загрузка игры", "Об авторских", "Выход", drawn directly over the splash photo with no panel
    behind them. Nothing in lang.dat contains those digit strings, which is consistent: they are not there.

The tactical modal trio (`tact_deploy_confirm`, `tact_quit_confirm`, `tact_modal_statusline`) was looked at at 3x
(`view159/zoom_tact_*.png`) because its body looked see-through and its text empty. All three follow the IDB:
`UiModal_DrawDialog @ 0x264A0` starts with `SoftSurf_BlitSrcRect(x, y, w, h, 0xFF, 255, 255, key 128)` over the whole
dialog rect -- a 128/256 blend of the map under it, which is what the translucency is -- then paints only the 2-px frame
edges at key 200 (0x26564 top, 0x26588 left, 0x265B0 bottom), the height rule is `13*nLines + 10`, `+35` when either
button label exists (0x264E8), centred. The Latin "YES"/"NO" are the original's own literals at 0xA62E8 / 0xA62EC. The
empty body at the first frame is the original's diagonal reveal (the 0x26E14 note: line i starts at character i, count =
cursor - line - 1) with a fresh cursor, not a missing text draw. IDB: line comment at 0x26538.

Family notes from the pass (no defect found in any of them): the crashdisk family is 100% lit with its own teal pad
art dominant (0,85,90); the buysell/hangar/hold family is fully painted with the hangar's empty middle strip already
closed as the `bCanDogfight` gate; the research and manuf families, the newbase family, the options/save-slot family and
the ufopaedia family each show their pad plus their panels with no orphan rect; the tact play family shows the iso map
with the modal text over it (the diagonal reveal again) and the "Hidden Movement" banner where the status-banner arm
runs; the e2e chain (mainmenu -> newgame -> base placed -> base screen -> geoscape -> after save -> menu at boot ->
slot list -> after load -> tact unit panel) reads as one continuous flow with no frame out of place.
