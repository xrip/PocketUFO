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
