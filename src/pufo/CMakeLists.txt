add_library(pufo_platform STATIC
    platform/pufo_gx.c
    platform/pufo_reg.c
)
target_include_directories(pufo_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pufo_platform PUBLIC pufo_options user32 gdi32)

add_library(pufo_util STATIC
    util/pufo_ptrarray.c
    util/pufo_rand.c
    util/pufo_str.c
)
target_include_directories(pufo_util PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pufo_util PUBLIC pufo_options)

add_library(pufo_dat STATIC
    dat/pufo_dat.c
    asset/pufo_asset.c
)
target_include_directories(pufo_dat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pufo_dat PUBLIC pufo_options pufo_util)

add_library(pufo_render STATIC
    render/pufo_render.c
)
target_include_directories(pufo_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pufo_render PUBLIC pufo_options pufo_dat pufo_util)

# Options/registry layer. Options_SaveToRegistry @ 0x114d8 reads the option
# block; Reg_SetString @ 0x9ed0c / Reg_GetString @ 0x9edbc persist it.
add_library(pufo_game STATIC
    game/pufo_options.c
    game/pufo_mapcell.c
    game/pufo_font.c
    game/pufo_unit.c
    game/pufo_tactstate.c
    game/pufo_path.c
    game/pufo_los.c
    game/pufo_boot.c
    game/pufo_template.c
    game/pufo_rmp.c
    game/pufo_tactctl.c
    game/pufo_turn.c
    game/pufo_save.c
    game/pufo_roster.c
    game/pufo_tactui.c
    game/pufo_timer.c
    game/pufo_menu.c
    game/pufo_statusmsg.c
    game/pufo_enemyai.c
    game/pufo_soldiername.c
    game/pufo_input.c
    game/pufo_cursordir.c
    game/pufo_isodraw.c
    game/pufo_baseui.c
    game/pufo_clock.c
    game/pufo_ship.c
    game/pufo_strateg.c
    game/pufo_geoscape.c
    game/pufo_ballist.c
    game/pufo_combat.c
    game/pufo_vistry.c
)
target_include_directories(pufo_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pufo_game PUBLIC pufo_options pufo_platform pufo_render pufo_dat pufo_util)

# UI panel line engine: the shared text/row engine every screen draws through.
# UiPanel_ctor @ 0x3e730 is 7092 bytes; see the header for the layout proof.
add_library(pufo_ui STATIC
    ui/pufo_panel.c
    ui/pufo_control.c
    ui/pufo_modal.c
)
target_include_directories(pufo_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pufo_ui PUBLIC pufo_options pufo_game pufo_render pufo_util)

add_library(pufo_audio STATIC
    audio/pufo_audio.c
)
target_include_directories(pufo_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pufo_audio PUBLIC pufo_options pufo_dat pufo_util)

add_executable(pocketufo
    app/pufo_app.c
)

# Parity tests. These read the shipped main.dat / lang.dat and compare against
# values extracted independently from those files, so a mismatch means the
# ported reader is wrong. Console apps, so WIN32_EXECUTABLE stays off.
add_executable(test_data ${CMAKE_SOURCE_DIR}/tests/test_data.c)
target_link_libraries(test_data PRIVATE pufo_options pufo_dat pufo_util)
set_target_properties(test_data PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_data
    COMMAND test_data
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_executable(test_render ${CMAKE_SOURCE_DIR}/tests/test_render.c)
target_link_libraries(test_render PRIVATE pufo_options pufo_render)
set_target_properties(test_render PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_render
    COMMAND test_render
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Audio game-side logic (naming, sample scan, option gating) with a fake
# backend, so it needs no sound hardware.
add_executable(test_audio ${CMAKE_SOURCE_DIR}/tests/test_audio.c)
target_link_libraries(test_audio PRIVATE pufo_options pufo_audio pufo_game pufo_dat)
set_target_properties(test_audio PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_audio
    COMMAND test_audio
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Map cell and geometry. Pure logic, no assets or hardware needed.
add_executable(test_mapcell ${CMAKE_SOURCE_DIR}/tests/test_mapcell.c)
target_link_libraries(test_mapcell PRIVATE pufo_options pufo_game pufo_platform)
set_target_properties(test_mapcell PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_mapcell
    COMMAND test_mapcell
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Font: .spr header, RLE glyph decode, and the advance table, checked against
# the shipped FONT.SPR / FONTSIZE.DAT.
add_executable(test_font ${CMAKE_SOURCE_DIR}/tests/test_font.c)
target_link_libraries(test_font PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_font PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_font
    COMMAND test_font
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Soldier predicates, facing sectors and 2x2 placement of large units.
add_executable(test_unit ${CMAKE_SOURCE_DIR}/tests/test_unit.c)
target_link_libraries(test_unit PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_unit PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_unit
    COMMAND test_unit
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Tactical action queue: append order, interrupt-on-death insert, and the
# uTuSpent >= uHp completion rule.
add_executable(test_tactstate ${CMAKE_SOURCE_DIR}/tests/test_tactstate.c)
target_link_libraries(test_tactstate PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_tactstate PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_tactstate
    COMMAND test_tactstate
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Reachability flood fill: cost composition, blocking, cost limit.
add_executable(test_path ${CMAKE_SOURCE_DIR}/tests/test_path.c)
target_link_libraries(test_path PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_path PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_path
    COMMAND test_path
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Voxel occupancy and line-of-sight: row-mask bit addressing, wall/floor/ceiling
# and occupant blocking, plus the trace. Uses the real mask tables when present.
add_executable(test_los ${CMAKE_SOURCE_DIR}/tests/test_los.c)
target_link_libraries(test_los PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_los PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_los
    COMMAND test_los
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# End-to-end startup load against the shipped archives: objects/fontsize/texts,
# the 240x320 surface, class.pal and the three pads.
add_executable(test_boot ${CMAKE_SOURCE_DIR}/tests/test_boot.c)
target_link_libraries(test_boot PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_boot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_boot
    COMMAND test_boot
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Mission templates (.ma1): header geometry, cell indexing, sprite-slot rule and
# application to the cell grid.
add_executable(test_template ${CMAKE_SOURCE_DIR}/tests/test_template.c)
target_link_libraries(test_template PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_template PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_template
    COMMAND test_template
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Route/spawn nodes (.rmp): record geometry, the truncating node count, link
# sentinels, and spawn-node acceptance.
add_executable(test_rmp ${CMAKE_SOURCE_DIR}/tests/test_rmp.c)
target_link_libraries(test_rmp PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_rmp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_rmp
    COMMAND test_rmp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Tactical cursor bounds and game-speed/timer control.
add_executable(test_tactctl ${CMAKE_SOURCE_DIR}/tests/test_tactctl.c)
target_link_libraries(test_tactctl PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_tactctl PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_tactctl
    COMMAND test_tactctl
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# UI panel line engine: the 7092-byte layout, defaults, meta triples, scroll
# clamping and word wrap.
add_executable(test_panel ${CMAKE_SOURCE_DIR}/tests/test_panel.c)
target_link_libraries(test_panel PRIVATE pufo_options pufo_ui pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_panel PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_panel
    COMMAND test_panel
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Tactical turn state machine: the bBusy x nPhase transitions, the opportunity-fire
# walk, and begin/end enemy turn.
add_executable(test_turn ${CMAKE_SOURCE_DIR}/tests/test_turn.c)
target_link_libraries(test_turn PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_turn PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_turn
    COMMAND test_turn
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Save-file container and soldier record round-tripping. The 740-byte prefix size
# is the layout check: every field is transcribed from the write order.
add_executable(test_save ${CMAKE_SOURCE_DIR}/tests/test_save.c)
target_link_libraries(test_save PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_save PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_save
    COMMAND test_save
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# XCom roster navigation: the three advance policies and the shared search loop.
add_executable(test_roster ${CMAKE_SOURCE_DIR}/tests/test_roster.c)
target_link_libraries(test_roster PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_roster PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_roster
    COMMAND test_roster
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Tactical action-mode / UI-screen / inventory-filter transitions.
add_executable(test_tactui ${CMAKE_SOURCE_DIR}/tests/test_tactui.c)
target_link_libraries(test_tactui PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_tactui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_tactui
    COMMAND test_tactui
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Win32 timer set: the ids and intervals, verified through an injected backend.
add_executable(test_timer ${CMAKE_SOURCE_DIR}/tests/test_timer.c)
target_link_libraries(test_timer PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_timer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_timer
    COMMAND test_timer
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Options-menu button handlers: speed/gamma/volume pads and their per-group quirks.
add_executable(test_menu ${CMAKE_SOURCE_DIR}/tests/test_menu.c)
target_link_libraries(test_menu PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_menu PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_menu
    COMMAND test_menu
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# On-screen status message stack: the two-threshold push policy and the draw walk.
add_executable(test_statusmsg ${CMAKE_SOURCE_DIR}/tests/test_statusmsg.c)
target_link_libraries(test_statusmsg PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_statusmsg PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_statusmsg
    COMMAND test_statusmsg
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# UiControl button widget: the mode/state pad-selection machine.
add_executable(test_control ${CMAKE_SOURCE_DIR}/tests/test_control.c)
target_link_libraries(test_control PRIVATE pufo_options pufo_ui pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_control PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_control
    COMMAND test_control
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Enemy AI decision predicates: the multi-target type table and the random turn.
add_executable(test_enemyai ${CMAKE_SOURCE_DIR}/tests/test_enemyai.c)
target_link_libraries(test_enemyai PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_enemyai PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_enemyai
    COMMAND test_enemyai
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Soldier name editing: backspace, copy-to-edit and grid-glyph append.
add_executable(test_soldiername ${CMAKE_SOURCE_DIR}/tests/test_soldiername.c)
target_link_libraries(test_soldiername PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_soldiername PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_soldiername
    COMMAND test_soldiername
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Tactical/geoscape directional input handlers.
add_executable(test_input ${CMAKE_SOURCE_DIR}/tests/test_input.c)
target_link_libraries(test_input PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_input PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_input
    COMMAND test_input
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Tactical cursor-direction animation state machine.
add_executable(test_cursordir ${CMAKE_SOURCE_DIR}/tests/test_cursordir.c)
target_link_libraries(test_cursordir PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_cursordir PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_cursordir
    COMMAND test_cursordir
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Modal yes/no dialogue state.
add_executable(test_modal ${CMAKE_SOURCE_DIR}/tests/test_modal.c)
target_link_libraries(test_modal PRIVATE pufo_options pufo_ui pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_modal PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_modal
    COMMAND test_modal
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Per-cell isometric glyph draw helpers and wall-palette selection.
add_executable(test_isodraw ${CMAKE_SOURCE_DIR}/tests/test_isodraw.c)
target_link_libraries(test_isodraw PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_isodraw PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_isodraw
    COMMAND test_isodraw
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Base-screen scratch-cell helpers.
add_executable(test_baseui ${CMAKE_SOURCE_DIR}/tests/test_baseui.c)
target_link_libraries(test_baseui PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_baseui PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_baseui
    COMMAND test_baseui
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Geoscape clock: decode, reset, and the minute/hour/day/month tick gates.
add_executable(test_clock ${CMAKE_SOURCE_DIR}/tests/test_clock.c)
target_link_libraries(test_clock PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_clock PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_clock
    COMMAND test_clock
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Player craft ticks: range recovery, refuel rates, supply consumption.
add_executable(test_ship ${CMAKE_SOURCE_DIR}/tests/test_ship.c)
target_link_libraries(test_ship PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_ship PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_ship
    COMMAND test_ship
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Contact list: proximity detect, radar detect, intercept expiry.
add_executable(test_strateg ${CMAKE_SOURCE_DIR}/tests/test_strateg.c)
target_link_libraries(test_strateg PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_strateg PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_strateg
    COMMAND test_strateg
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# The geoscape tick chain and its wiring to the clock.
add_executable(test_geoscape ${CMAKE_SOURCE_DIR}/tests/test_geoscape.c)
target_link_libraries(test_geoscape PRIVATE pufo_options pufo_game pufo_render pufo_dat pufo_util)
set_target_properties(test_geoscape PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_geoscape
    COMMAND test_geoscape
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Aim block, projectile arc and the throw solver.
add_executable(test_ballist ${CMAKE_SOURCE_DIR}/tests/test_ballist.c)
target_link_libraries(test_ballist PRIVATE pufo_options pufo_game pufo_util)
set_target_properties(test_ballist PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_ballist
    COMMAND test_ballist
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)




# Shot resolution: the 41x41 splash window, cell damage, sprite breakage.
add_executable(test_combat ${CMAKE_SOURCE_DIR}/tests/test_combat.c)
target_link_libraries(test_combat PRIVATE pufo_options pufo_game pufo_util pufo_audio)
set_target_properties(test_combat PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_combat
    COMMAND test_combat
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_executable(test_vistry ${CMAKE_SOURCE_DIR}/tests/test_vistry.c)
target_link_libraries(test_vistry PRIVATE pufo_options pufo_game pufo_util)
set_target_properties(test_vistry PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_vistry
    COMMAND test_vistry
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_executable(test_fov ${CMAKE_SOURCE_DIR}/tests/test_fov.c)
target_link_libraries(test_fov PRIVATE pufo_options pufo_game pufo_util)
set_target_properties(test_fov PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_fov
    COMMAND test_fov
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_executable(test_vis ${CMAKE_SOURCE_DIR}/tests/test_vis.c)
target_link_libraries(test_vis PRIVATE pufo_options pufo_game pufo_util)
set_target_properties(test_vis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_vis
    COMMAND test_vis
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_executable(test_blast ${CMAKE_SOURCE_DIR}/tests/test_blast.c)
target_link_libraries(test_blast PRIVATE pufo_options pufo_game pufo_util)
set_target_properties(test_blast PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/tests
)
add_test(NAME test_blast
    COMMAND test_blast
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)





# The original is a GUI app (WinMain @ 0x9fcc4), so there is a wWinMain entry
# point and no console. Without this the CRT looks for main().
set_target_properties(pocketufo PROPERTIES WIN32_EXECUTABLE TRUE)

target_link_libraries(pocketufo PRIVATE
    pufo_options
    pufo_platform
    pufo_util
    pufo_dat
    pufo_render
    pufo_game
    pufo_audio
)
