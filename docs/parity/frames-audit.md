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
2. `econ_buy_craft_queued` / `econ_hangar_cap_full` -- rows 88..120 are a filled panel (0x5A002C..0x620834), and a bright
   magenta frame box sits over the panel header row with no text in it. Needs the BuySell panel's own row/highlight arm
   (`BuySellScreen_Draw @ 0x93DEC`, the scroll list) and the stored cursor row the fixture left behind.
3. `econ_hangar_combat` -- does the original's hangar arm put controls in the bottom strip in this state?
4. `tact_hud_*` and `tact_briefing*` / `debrief_banner` -- thin bands at rows 1..16 and 311..319: compare against the
   tact HUD draw arms' pad rects before judging.
5. `tact_hud_minimap` rows 16..63 -- a 48-row hole in a live-looking frame, the biggest remaining one.
6. The 28 tact `DumpFrame` sites still have no recorded draw call, so their classification is not trustworthy yet.
