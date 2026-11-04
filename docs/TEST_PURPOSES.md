# Test purpose catalog

This catalog covers all 67 tests registered by CMake. Each entry links its source and states the behavior checked, fixture level, and important limits.

Scope labels: local tests call production functions with prepared structs, cells, callbacks or files. World tests use real world owners and shipped data but stage parts of the scenario. App tests use the shell or window path. A pass covers only the stated inputs and assertions.

Two current findings show why fixture level matters. TestSlotList supplies its
own clock and state pointers, so its correct text and side-effect assertions
could not catch App binding the formatter to scratch instead of live World
owners. Likewise, a World_NewGame followed by World_LoadSlot retains options
callbacks and cannot prove a fresh startup load installs them. The App owner
repair has raw/peer and compile evidence. The fresh-consumer Options regression
now compiles: it opens Options through World pointer dispatch and checks save/load
tab binding without a preceding NewGame on that consumer. Its supplied formatter
does not prove the private App hook. No assertion or native flow pass is claimed.

## Verification status

Pending live-fade cases: `TestPackRgb565Pair` changes the same pointed-to fade
cell from10 to9 and checks raw modal colors0x0B06 then0x0183. World save tests
check NewGame binds the shared color environment to World.nMapUiSlot1. Before
LoadSlot, the fixture clears that pointer; the load must bind it again rather
than inheriting the prior NewGame result. These are local color/World owner
checks, not a pumped inventory-error -> battle-end -> modal-frame scenario.
They are unrun and await the next combined build.

Latest connected-fix cases compile in Debug and Release but remain unrun:
`TestWorldManufCraftCount` spawns a real World craft, then uses the live
manufacturing selection/+ callbacks to check full and free hangar capacity.
It stages finished hangars and may expose a valid product row without research;
it does not prove unlock or hourly production. `TestFallActionRosterCountRefresh`
uses one FallActionCtx/SideLists for two panic transfers with no resync between
calls; it checks both victims remain in the live enemy roster. It does not drive
the complete AI/tick loop. `TestSignedAdvanceSpacing` checks width and actual
glyph positions for a synthetic -2 advance; shipped advances are0..14, so this
checks the signed format contract rather than a visible shipped-data failure.

Latest UI join cases, compiled only: the World Buy/Sell test presses a real
scroll control from phase0, pumps two pulses and checks one repeat, then releases
and checks no further repeat. The direct pulse test now preserves0->1 naturally;
old forced-phase1 fixtures could not catch the unconditional-reset bug. The
World modal fixture draws two buttons then one and uses pointer wrappers over
the retained second region. It prepares modals and supplies an end hook, so it
does not prove native quit/inventory actions. See
[repeat/modal scope](parity/ui-repeat-modal-review-2026-10-29.md). All remain unrun.

Latest snapshot, compiled only: separate counter storage and a screen13 banner
regression catch the old fade snapshots; the prior alias test could not. The
generic cursor9->10 case is separate from World, which already ticks before
snapshot construction. GameTime_Pause now tests the strategic clock, preserving
total/stamp/date, rather than an unrelated speed object. The NULL-counter test
no longer holds a pointer to an expired cursor.

Loading mode3 tests now share the forced40 cursor with the modal renderer and
expect final1/progressive0, as raw stores require. They check panel/text ink and
no-button layout output. They still inject a local view, so native App loading
panel publication is source/peer evidence, not an executed App test. See the
[loading review](parity/loading-modal-review-2026-10-29.md) and
[frame/clock review](parity/frame-fade-clock-review-2026-10-29.md). All remain unrun.

Byte-model correction, compiled only: the cancel field is u8 bSkipFireShots,
not signed step delta. Enqueue tests now check flag1->0 and reset timing; the
former upper-word sentinel assertion did not describe a logical original field
and was removed. Independent queue nDelta tests remain. Native input/clock/status
reviews add source evidence, not an executed app-flow pass. See
[width/type proof](parity/tact-fire-flag-type-2026-10-28.md) and
[native route limits](parity/native-input-clock-review-2026-10-28.md).

Fire/finance/AI batch, compiled only: test_tactstate checks pre-TU clears versus
success-only count reset, a byte cancel flag, and two successive ammo-drain
groups for sort13/35 without manually resetting the shot counter. It does not
pump full projectile physics. The World reaction argument has raw/source proof;
no new native reaction scene was run. See [contract and limits](parity/fire-burst-contract-2026-10-28.md).
The monthly probe changes valid count prefixes over real draws, checks adapter
upkeep against live-count upkeep, and verifies restoration so an initially-zero
cache cannot pass. It does not read warning pixels or force bankruptcy; nonzero
site delta coverage is conditional. See [monthly scope](parity/monthly-projection-counts-2026-10-28.md).
Both full incremental builds pass; all new assertions remain unexecuted.

Enemy-turn review: the existing turn-return/TU checks can pass without proving
an ordinary enemy command. The raw/source handoff and pre-AI roster refresh are
checked. New TestOrdinaryEnemyActionWorld requires an actor-owned TURN/STEP in a
controlled scene before pumping, then queue drain/input return. It compiles but
has not run. Native movement, fire and actual app timer progress remain required. See
[bounded AI review](parity/enemy-turn-handoff-2026-10-28.md).

TACTVER World binding correction: allocation and later item reading now share
the loaded selector. Direct flobjs tests supply this flag themselves and cannot
prove that World wiring. The current world roundtrip uses TACTVER1; the new
World clone uses TACTVER0 and a one-byte count with unchanged17-byte records.
It checks nonempty item fields and both link namespaces, rejects pools outside
1..255, runs after RNG-sensitive scenarios, and restores slot64 last. No new
compatibility test has run. Both full incremental builds compile cleanly; this
does not establish an assertion pass. See [raw context proof and fixture limits](parity/flobjs-count-selector-2026-10-28.md).

Research/manufacture fixture correction, not executed: the section99 scenario
must find both the product ID and its completed topic ID in the offered panels.
It previously replaced the intended product with row0, which could pass by
manufacturing an unrelated item. Later cost/component/stock checks now retain
the exact product. Topic selection depends on loaded fixture state; no fixed
topic29/item20 result is claimed. The scenario calls the hourly hook directly,
so it does not prove normal elapsed-time/app-pulse scheduling.

Load-time owner correction, not executed: TestLoadGame requires the time
chunk's trailing word to replace a distinct live sentinel. The legacy-load
fixture checks it before mission-context refresh/migration. TestTime alone
checks file bytes and could not detect this missing World state transfer.
See [raw evidence and scope](parity/load-time-live-word-2026-10-28.md).

Mission-return correction, not executed: test_ship now checks the original
zero=announce/nonzero=suppress ammo-message contract and full-word refresh state.
Its stores-message case also changes the live intercept index between calls
and checks that C0 follows its low byte, rather than remaining zero or stale.
TestArrivalDeliversLootOnce in test_strateg now removes a prepared mission
target, issues flag27 through the production return handler, and uses the
computed duration to check one minute before/at arrival. It then connects the
production event pass to loot delivery: correct home-base stock, one warning, pending flag consumed,
no duplicate delivery on another event pass, and no direct transfer for a cold
craft. It prepares the contexts and disables replacement spawning; it does not
prove the World adapter, battle-end/shutdown, native input, app clock or save/load.
See [return-order fixture scope](parity/return-order-scheduling-2026-10-28.md) and
[raw evidence and exact scope](parity/mission-return-handoff-2026-10-28.md).

Loot-index correction, not executed: marker assertions now require the home-base
block. TestLootBaseSelection separates craft index0, type1 and home base2, and
checks the home marker and used-storage-needed flags. The original negative-site
incoming gate is [statically proved zero](parity/incoming-capacity-gate-2026-10-28.md).
The existing local test_geoscape assertion checks this zero result despite live
site cargo; it does not prove the original scratch-memory write invariant or
the native game flow. See
[raw trace and current candidate](parity/loot-return-indexing-2026-10-28.md).

Base compaction correction, not executed: the demolition fixture now requires
the removed manufacturing row to take its successor, while preserving the prefix,
discarded final storage row and out-of-count sentinel. Its former expectation
encoded a false original-bug claim; raw register/offset algebra, not a failing
test, justified the change. See [evidence/current candidate](parity/base-removal-compaction-2026-10-28.md).

Staff-row update, not executed: Buy/Sell and Transfer panel cases distinguish
base0 assignments from base1 assignments and require exact free-staff counts.
The editor case requests more staff than are free and checks the sale limit,
then checks that fully assigned staff cannot be sold. Both full default build
configurations compile; runtime multi-base/commit/save-load behavior is pending.
See the [raw contracts, test data and current manifest](parity/staff-base-row-2026-10-28.md).

All67 test executables have current full default Debug/Release builds with zero
diagnostics. None was executed. The earlier clean-build manifest describes its
prior source; current identities are in the staff-row report. Compilation is not
a test pass.

Startup Back repair: existing TestEmptyBtnClick checks only helper forwarding
and callback order. It does not inspect the real App callback's mode store and
would pass with the old App no-op. The current App fix compiles; the actual
native Back/reopen/load flow remains pending. See the
[repair and coverage limit](parity/startup-list-back-2026-10-28.md).

Audio-bank update, not yet executed: TestNullSampleSlot injects a null backend
handle at real empty slot18, uses distinct handles, and checks that slot34 still
plays with its own handle while18 remains null. Load-attempt/index counts prove
bank continuation, not decoding or device output. See the
[shipped resource inventory and limits](parity/audio-resource-bank-2026-10-28.md).

Shared-window/phase repair, not yet executed: test_combat's direct, ceiling-break
and timed-item cases use bound Map-like windows with null explicit buffers.
TestAreaFirePhaseWorld owns a fresh mission, calls its installed area callback
with opposing Map+1/action-latch flags, and checks the correct counter reset
while preserving the unrelated slot. These are storage/binding checks; native
explosion/phase progression still needs host verification. See
[current evidence](parity/shot-map-windows-2026-10-28.md).

Shot-owner repair, not yet executed: test_combat distinguishes the local weapon
cache from the persistent owner, for a direct hit and a due timed explosive.
Each owner starts at a different object and must receive the object actually
resolved. Existing damage/countdown/removal checks remain. The World bindings
and native item-use sequence still need runtime checks. See the
[repair and candidate evidence](parity/item-use-shot-owner-2026-10-28.md).

Control ownership repair, not yet executed: test_control now checks Debug heap
balance for three pad records plus their pixel buffers. Bank/exit/research
fixtures pass owned allocations into owning cleanup and no longer assert that
deleted records survive. See [raw evidence and test scope](parity/control-pad-ownership-2026-10-28.md).

New checks awaiting execution: `test_tactinit` starts with invalid craft
coordinates and checks that crew selection and later deployment receive the
generated placement. `test_terrain` changes the craft sprite base in the loading
hook, then checks flag changes and an actual map wall stamp in the new group.
These cover stale caller bindings and the resulting craft patch; they do not
prove saved mission gameplay. Both targets compile in Debug/Release. See the
[repair evidence](parity/mission-placement-refresh-2026-10-28.md).

- The latest combined Debug and Release matrix passed 67/67 in each, with zero build warnings/errors. It includes the live-XCOM-count repair and corrected real-hook fixture; App source is `729A8BAD5D87DEF6297E6E299F12581A1FC46A74C6029F281F1EF29A55B741E2`. See the [current result](../.workers/luna-turn-count-finish/runtime-isolated-20261027T125119Z-2a1e2dcc/RESULT.md) and [frozen source/binary manifest](../.workers/luna-economy-rawkit-audit/manufacture20-source-freeze-20261027T131029Z-506be9eb/ARTIFACT-MANIFEST.json). Later staged-exit edits are not covered by that gate.
- A separate scoped Debug run passed `test_unit` and `test_tactstate` 2/2, confirming the footprint edge assertions. It used the same linked binaries as the full matrix and did not rebuild them. See the [scoped result](../.workers/luna-runtime-footprint-gate/ctest-3578b9ebb2b545f6885d325adc2c5436/RESULT.md).
- A later App-only source snapshot (`pufo_app.c` SHA256 `1C7915594813324B802250095D0357DB21C6B5F29684632439503D4387970C50`) passed `test_appshell` 1/1 in Debug and Release. It checks the signed view-cell index against target and decoy cells, keyboard routes, and focus without selection changes. The full 67-test matrix was not rerun on that App source. See the [focused result](../.workers/luna-runtime-appgate/retry-fixed-3c17170d4a914f00b415f514ad3b2dd5/RESULT.md) and [hook note](parity/app-key-hooks-2026-10-27.md).
- Later App71BF builds and AppShell checks pass in both profiles. The [retained fight](../build/e2e_victory_outside_plane/20261027T164634Z-462/RESULT.md) reaches victory in4 turns/40 actions and writes slot0 through real prep, approach and visible-target firing. The original runner failed in its checker; [complete corrected cached validation](../.workers/luna-runtime-victory-current/cached-check-20261027T164634Z-462-8A1D/RESULT.md) passes without game replay or changed inputs. Fixture and live-flow evidence remain distinct.
- NewGame render-hook wiring is connected, but public Options-row draw-before-payload-I/O remains unverified. Native New Game starts placement pane2, so the probe must finish native placement before using the map's Options control. No direct script save/load step is a substitute for the public rows.
- The earlier 65/66, 66/67 and 67/67 run history is kept in the [preserved guide](../.workers/luna-frame-scenario-isolation/TEST_PURPOSES-before-catalog.md).

## Archive, asset and save readers (9)

- [test_data.c](../tests/test_data.c) checks archive open, member lookup, archive priority, sizes and bounded reads. Its new file-contract cases use owned temporary inputs to check the raw read-only predicate, create/read peer-sharing rules, guarded mode3 failure without replacing existing data, and partial-mode handle cleanup. These assertions compile but remain unrun; see [exact contract and limits](parity/datfile-contract-review-2026-10-29.md).
  Scope: shipped main.dat and lang.dat; no game session.
- [test_texts_lang.c](../tests/test_texts_lang.c) checks the TEXTS member size, record stride, selected text values and archive source.
  Scope: shipped language data; no screen rendering.
- [test_boot.c](../tests/test_boot.c) runs Boot_LoadStartup and checks object counts, allocated tables and selected object records.
  Scope: shipped archives; no New Game controls.
- [test_font.c](../tests/test_font.c) reads FONT.SPR and FONTSIZE.DAT, checks the glyph index and decodes RLE glyph data.
  Scope: shipped files and local decode buffers.
- [test_template.c](../tests/test_template.c) reads MA1 headers, tile dimensions and single-level/bounds cases.
  Scope: shipped map templates; no tactical mission.
- [test_terrain.c](../tests/test_terrain.c) builds terrain cells and checks layer-based plane floors, map flags and template effects.
  Scope: controlled map arrays and asset hooks; not a full mission.
- [test_rmp.c](../tests/test_rmp.c) checks RMP record layout, links, modules and tile offsets using shipped and short synthetic files.
  Scope: route-map parser; no unit movement.
- [test_save.c](../tests/test_save.c) checks slot paths, temporary save chunks, soldier fields, record sizes, and SaveGame/LoadGame driver joins and legacy migration hooks.
  Purpose: catch wrong field widths/order and missing transfers from loaded records to live owners. Scope: prepared data and injected hooks; not the save menu, current runtime acceptance, or compatibility with an original WinCE save. Reading a modern chunk with an old version flag tests its shifted read offsets, not a valid old-format file.
- [test_save_world.c](../tests/test_save_world.c) checks 257 FLOBJS records and a valid 6x5 World_SaveSlot/World_LoadSlot round trip. It verifies 12,000 cell records across four layers, a positive XCOM wire ordinal and owner, and three 0x7FFF origin placeholders. The tactical load also checks that prior live drag/repeat/edge state resets, slot4 becomes200, and the saved deploy flag remains1. These assertions catch omitted live reset writes and a blanket input clear; they remain unrun.
  Scope: fresh declared world fixtures and port saves, not original-save compatibility or the save menu.

## Records, map math and game rules (27)

- [test_ptrarray.c](../tests/test_ptrarray.c) checks insert order, negative/out-of-range insertion, removal bounds and the remove-all marker.
  Scope: local pointer arrays.
- [test_rand.c](../tests/test_rand.c) checks Game_RandU16 scaling and proves a zero bound still consumes a random draw.
  Scope: fixed random stream.
- [test_constants.c](../tests/test_constants.c) pins shared PUFO constants to literals so header drift breaks the test.
  Scope: compile-time constant census; it does not independently read IDA.
- [test_mapcell.c](../tests/test_mapcell.c) checks Mc1Rec and MapCell offsets plus cell flag and occupant helpers.
  Scope: local cell records.
- [test_mapctx.c](../tests/test_mapctx.c) checks GameMap cell lookup bounds and move-action context wiring, including status hooks.
  Scope: local map/context structs.
- [test_tactstate.c](../tests/test_tactstate.c) checks queue helpers and move processing, including refusal of a large-unit step when a wrapped layer-1 footprint cell is occupied. Its credit fixture starts with dword0x10003 and checks low-half payout plus full-dword clear; this prepared-state check alone does not prove that a kill produces credit.
  The new throw regression drives the move action with scatter and checks that saved travel comes from the second landing walk, alongside aim, TU, queue, hand and item state. The same test compiles with both cores: old Debug/Release fail only that travel assertion; repaired Debug/Release pass. [Proof and limits](parity/post-scatter-throw-resolver-2026-10-27.md).
  Scope: local queue and cell fixtures; no player input.
- [test_unit.c](../tests/test_unit.c) checks Soldier layout, predicates, battle reset, armor values and large-unit placement, movement, death, corpse drops and the type-18 blast at the fixed 60-cell row edge. The blast fixture now distinguishes the original neighbor object0 selector from the former object17 choice through linked-body HP and removal; it compiles but remains unrun. See [its exact scope](parity/death-damage-selector-2026-10-28.md).
  Scope: controlled 4x60x60 cell grid; it does not prove the original runtime populated those layer-1 table slots or model a real mission.
- [test_moveaction.c](../tests/test_moveaction.c) advances action-queue commands through turn, step, recover and fire state paths.
  Scope: controlled queue and hooks; no player screen.
- [test_path.c](../tests/test_path.c) checks facing deltas and clockwise/counter-clockwise turn choice.
  Scope: direction math only; it is not route finding.
- [test_step.c](../tests/test_step.c) checks step occupancy probes, vertical re-anchoring, destination layers and floor rules.
  Scope: local cells and step callbacks.
- [test_los.c](../tests/test_los.c) checks LOS bit order and voxel traces against walls and occupants.
  Scope: prepared cells; not a live tactical shot.
- [test_tactctl.c](../tests/test_tactctl.c) checks map-cursor bounds and GameSpeed_Set behavior with timer hooks.
  Scope: local control and timer state.
- [test_turn.c](../tests/test_turn.c) checks tactical phase transitions, callback order and opportunity fire. Its control-return case calls real BeforeEnemyStep, returns a controlled human to XCOM, and requires that newly live unit's AI/spotting state to reset in the same turn.
  Scope: a controlled TactCtx and real pre-step hook; not End Turn from a live screen. [Old-core run](../.workers/luna-turn-count-finish/runtime-isolated-20261027T125119Z-2a1e2dcc/logs/old-ctest-debug-red.log) compiles and misses the reset; the identical fixture passes with the new core in Debug and Release. An earlier invalid fixture assignment was corrected; its compile failure is not counted as evidence of the game defect.
- [test_tactend.c](../tests/test_tactend.c) checks mission shutdown frees, retained owners, outcome cleanup and battle tallies.
  Scope: prepared world records and free hooks; not a complete debrief.
- [test_roster.c](../tests/test_roster.c) checks roster search, wraparound, filtering and skipping non-XCOM entries.
  Scope: local roster arrays; no soldier panel.
- [test_enemyai.c](../tests/test_enemyai.c) checks unit-type predicates, multi-target rules and selected AI turn decisions.
  Scope: prepared units and controlled random values.
- [test_clock.c](../tests/test_clock.c) checks time decode/reset and minute, hour, day and month boundaries.
  Scope: local game-time records.
- [test_ship.c](../tests/test_ship.c) checks range recovery, refuel rates, supply gates and fuel caps.
  Scope: local Ship/Base records; no geoscape click.
- [test_strateg.c](../tests/test_strateg.c) checks strategic list lookup, detection radii, radar states and intercept expiry.
  Scope: prepared strategic items and seeded rolls.
- [test_geoscape.c](../tests/test_geoscape.c) checks geoscape tick order across ships, bases, events and clock state. `TestGeneratedBaseLabelCapacity` joins the production formatter, base creator and save join at signed ordinals100/-128: it checks the full live name and unchanged20-byte save prefix. It compiles but remains unrun; see [scope and raw evidence](parity/base-name-width-2026-10-28.md).
  Its lookup cases now distinguish full signed SiteId keys from normalized byte keys, including negative values and256. The fresh two-base World fixture also checks the real normal callback and rejects key+256 without wrapping. These compiled checks do not establish campaign lifetime counts; see [the lookup contract](parity/base-search-contract-review-2026-10-29.md).
  Scope: injected callbacks and records; not a native timer session.
- [test_timer.c](../tests/test_timer.c) checks timer ids and intervals through an injected backend.
  Scope: timer calls only; it does not wait for Windows timers.
- [test_ballist.c](../tests/test_ballist.c) checks muzzle offsets, aim vectors, shot charging and projectile/throw arcs.
  Scope: synthetic geometry; not a shot from the UI.
- [test_combat.c](../tests/test_combat.c) checks hit-table indexing, splash windows and wall/sprite damage counters.
  Scope: a prepared map and hit table.
- [test_vistry.c](../tests/test_vistry.c) checks the 120-ray perimeter table and which rays and steps each pass activates.
  Scope: local ray arrays.
- [test_fov.c](../tests/test_fov.c) checks FOV clearing, range costs, row/column folding and retained field bits.
  Scope: fixture visibility arrays and map cells.
- [test_vis.c](../tests/test_vis.c) checks opportunity-fire selection, Unit_UpdateVisibility and kill/roster effects. Direct death fixtures now check queue credit, excluded types and the already-dead guard; they do not establish World queue completion.
  Scope: synthetic units and callback owners.
- [test_blast.c](../tests/test_blast.c) checks blast footprint distance, wall flags and occupied-cell inclusion.
  Scope: controlled cells; not grenade input.

## Drawing, audio and screen widgets (20)

- [test_render.c](../tests/test_render.c) checks SoftSurf allocation, dimensions, pixel access and drawing operations.
  Scope: software surface; no window.
- [test_audio.c](../tests/test_audio.c) checks game-side sample naming and call order with a fake backend, then tests shipped music decoding and available WinMM buffers.
  Scope: device playback is conditional; buffer completion is not proof of audibility.
- [test_tacthud.c](../tests/test_tacthud.c) checks kneel/climb costs, inventory/special-item hooks and HUD drawing.
  Scope: local actor and callback records.
- [test_titles.c](../tests/test_titles.c) checks backdrop pixels, scrolling fade and credit-line drawing.
  Scope: software surface; no title-screen input.
- [test_renderframe.c](../tests/test_renderframe.c) checks Render_Frame geoscape/menu dispatch and present/refcount hooks.
  Scope: direct frame environment.
- [test_mainmenu.c](../tests/test_mainmenu.c) checks MainMenu_Draw row and border pixels for selected/disabled states.
  Scope: local surface and menu state; not WndProc input.
- [test_cmdpads.c](../tests/test_cmdpads.c) checks tactical command-pad slot bounds and blit rectangles.
  Scope: draw hooks and pad records.
- [test_panel.c](../tests/test_panel.c) checks UiPanel layout, line capacity, text/meta, scrolling and click/hold counters.
  Scope: local panels; no full screen.
- [test_statusmsg.c](../tests/test_statusmsg.c) checks status-message capacity, busy-drop policy, TTL and draw order.
  Scope: local message stack.
- [test_control.c](../tests/test_control.c) checks UiControl defaults, enabled/mode/state gates and hit/blit selection.
  Scope: local widget and pad fixture.
- [test_uibank.c](../tests/test_uibank.c) checks descriptor groups, tactical-control bindings, pad loading and bank teardown.
  Scope: local widget bank and loader hooks.
- [test_menu.c](../tests/test_menu.c) checks speed, gamma, volume and pad changes through menu handlers.
  Scope: direct callbacks; not a player menu session.
- [test_modal.c](../tests/test_modal.c) checks modal begin/end, dialog drawing and pointer/key choice routing.
  Scope: prepared modal state and callbacks.
- [test_menubtn.c](../tests/test_menubtn.c) checks confirm/clear/secondary button callback and state transitions.
  Scope: local controls; not a live message pump.
- [test_isodraw.c](../tests/test_isodraw.c) checks wall-palette selection and unit glyph drawing.
  Scope: synthetic cell/sprite records.
- [test_baseui.c](../tests/test_baseui.c) checks base-screen scratch cells, openers, panel positions and cached selection values.
  Scope: local screen structs.
- [test_plane.c](../tests/test_plane.c) checks hangar weapon selection, store loading/unloading, row construction and craft inventory swaps.
  Scope: synthetic bases/crafts and hooks; not a full hangar player flow.
- [test_soldiername.c](../tests/test_soldiername.c) checks name backspace, edit-copy and glyph append helpers.
  Scope: local name buffers.
- [test_tactui.c](../tests/test_tactui.c) checks inventory filters, scrolling, visible-list merge/sort and radio selection.
  Scope: tactical UI state; no full mission input.
- [test_invdrag.c](../tests/test_invdrag.c) checks inventory hit regions and drag offsets for ground, pockets and hands. Its TU checks now distinguish the Map+1 free mode from the Map+2 action latch, including clip loads and split's unconditional6-TU precheck versus waived debit. It also checks the setup-equip context. These assertions compile but remain unrun; see [raw contract and scope](parity/inventory-tu-gate-2026-10-28.md).
  Scope: prepared item lists and hit-test context; not a live inventory session.

## Geoscape input, world setup and application (11)

- [test_geopointer.c](../tests/test_geopointer.c) checks pointer dispatch by geoscape screen/flag, control hit order and drag state.
  Scope: synthetic pointer environment; not a World or HWND.
- [test_smaphit.c](../tests/test_smaphit.c) checks screen-to-world hit coordinates, zoom scaling and strategic selection/pause flags.
  Scope: prepared map/list records.
- [test_closeui.c](../tests/test_closeui.c) checks per-screen close dispatch, resource frees and retained values.
  Scope: fake UI/world owners; not each full screen.
- [test_input.c](../tests/test_input.c) checks tactical/geoscape key and pointer dispatch, focus, camera bounds and action gating.
  Scope: supplied input hooks; not OS messages.
- [test_cursordir.c](../tests/test_cursordir.c) checks cursor direction and animation countdown state.
  Scope: local cursor fields.
- [test_geouimanage.c](../tests/test_geouimanage.c) checks GeoscapeUi_FreeAll timer kills, UI frees and bFreeWorld roster retention/release.
  Scope: allocated fixture owners and timer hooks.
- [test_geoiinit.c](../tests/test_geoiinit.c) checks Geoscape_InitUI state, controls and shipped pads through init hooks.
  Scope: world UI initialization, not a complete New Game.
- [test_geoscapeframe.c](../tests/test_geoscapeframe.c) drives real World pointer actions and End Turn. With seed 0, it checks 19 queued Mars enemy removals in 157 death ticks, then an in-tick restart that returns to a live type-4 map with reserve/hidden counts and dispatch/pending/frame-scratch fields at zero. It also checks the actual grenade pickup/prime/throw/landing/detonation chain and saved owner/cost/armor ledgers.
  The new initial-inventory pointer round trip checks free transfers before first commit; existing Part161 checks paid transfers afterward. The new hire fixture checks a base1 Soldier order through World clock pulses: still pending at23, delivered at24, no duplicate at25. Base construction is fixture setup, and native input/timer delivery is untested. Both connected checks compile but remain unrun; see the [inventory report](parity/inventory-tu-gate-2026-10-28.md) and [hire report](parity/purchase-clock-fixture-2026-10-28.md).
  The same helper now transfers that arrived Soldier from base1 to base0 through destination-first World input/draw and clock pulses. It checks unchanged ID/object, transit and arrival ownership, roster/funds stability and no duplicate. The older source-row/direct-commit case checks a stale-cache edge, not a dead picker. These compiled assertions remain unrun; see [transfer scope](parity/interbase-transfer-review-2026-10-29.md).
  Its revised106.b credit fixture uses a prepared completed-shot prefix, the real AP death gate, command8 and World ticks to check generated credit reaches an operative shooter and clears on queue drain. It is a nonterminal type22 transform; projectile damage, native input and terminal payout are outside its scope. The raw terminal-kill path skips payout. These assertions compile but remain unrun; see the [credit report](parity/kill-credit-owner-2026-10-28.md).
  Its name-entry scenario opens the keyboard through World pointer input, appends a glyph, removes it with Backspace, then accepts and checks the roster row. This proves more than local handler calls, but does not use HWND input.
  Its controlled PSI scenario runs real End Turn, AI, queue and fall handling, checks mode5 side transfer and actor link, then next-turn restoration and live counts. The observer forwards the actual AI callback; it does not force results. [Runtime trace and limits](parity/world-psi-control-2026-10-28.md).
  Scope: declared World fixtures and staged state; grenade damage and counter inputs are controlled, so this is not a played final campaign or a victory claim. Part 107 supplies only the host C352 scratch byte before real World draws; there is no native Win32 message pump.
- [test_startnew.c](../tests/test_startnew.c) checks Game_StartNew base, craft, soldiers, funds, ids and raw starting inventory.
  Scope: game-start state with hook inputs; not the start-menu button path.
- [test_tactinit.c](../tests/test_tactinit.c) checks Tact_InitMission fields, panels, briefing, deployment and cargo/save behavior.
  Scope: controlled mission assets and hooks; not a full sortie.
- [test_appshell.c](../tests/test_appshell.c) checks key routes through PocketUFO_WndProc and focus callbacks through the App hook table, including VK_BACK close, VK_SPACE promotion cancel, signed view-cell target-versus-decoy lookup and HUD focus without selection change.
  Scope: latest focused Debug and Release CTest passed 1/1 each; focus uses an Input_OnPointer fixture, not a full HWND mouse path. The Options-row render probe remains unrun.

## Runtime evidence separate from CTest

- The fixed semantic checker passed over an immutable captured normal-ground home run: 165/169 arrival pulses, transfer gate 1 with no shortfall, range 8592→9792, crew, stock and save/load. No new game was launched for this check. It reads saved world state and work, not transient display bytes: in that capture, load speed 5 with UI byte 0 did not erase saved time, pause, base or research progress; unused roster-array tail slots are not active soldiers. See the [home result](../.workers/luna-runtime-e2e-serial/home-hourly-scopefix-20261027-ab67b65e7f0943e2968660d3bd029b8f/RESULT.md).
- Transfer delivery and fresh-load stock/time retention now pass on recorded EE55; Research47 completes and the cold-loaded Research8 screen shows topic6. Manufacture produces item20 at hour44, charges31500, releases ten Engineers and passes a slot3 fresh load on recorded cc1. Exact runs and limits are in the [completion audit](COMPLETION_AUDIT.md).
- The [retained winning run's corrected cached check](../.workers/luna-runtime-victory-current/cached-check-20261027T164634Z-462-8A1D/RESULT.md) passes native victory in4 turns/40 actions, normal debrief/geoscape return, loot/cargo and slot0. The [A934 cold roundtrip](../.workers/luna-runtime-coldowners/cached-check-201804-A042/RESULT.md) also passes full tactical owner/terrain comparison and resumed actions. [Winning-save Home delivery/fresh loading](parity/victory-home-load-2026-10-28.md) and [isolated native Options Save/Load](parity/win64-focus-timers-2026-10-28.md) now pass within their named scopes. Independent original WinCE `.sav` compatibility and full-game completion remain open.
- The [current App audio run](../.workers/luna-audio-app-proof/runs/20261027T214116Z-842/AUDIO-RESULT.md) passes device/rate, completed nonzero music and generic SFX completion after one real menu-control input. It does not identify the SFX voice or prove amplitude/speaker audibility. The [winning-save Home countdown, delivery and final fresh load](parity/victory-home-load-2026-10-28.md) pass on retained4D. Its craft starts at home, so that fixture does not prove a return flight.

Full earlier run history is kept in the [preserved guide copy](../.workers/luna-frame-scenario-isolation/TEST_PURPOSES-before-catalog.md).
## Saved-game checks added on 2026-10-28

Pending [restart-layer lifetime verification](parity/mission-init-bindings-2026-10-28.md):
the existing queued Mars restart checks the deferred path's replacement header.
A direct end-check restart must also be exercised, because immediate shutdown
has a different ownership path. Source/compile checks do not establish that pass.

Pending [terrain construction regression](parity/terrain-construction-resolver-2026-10-28.md):
real World launch must apply cell damage and retain the final firing object.
Old Debug/Release fail both new assertions; repaired Debug passes them but
exposes the following PSI fixture's dependence on prior survivors. The new
independent PSI setup is compile-checked only. Earlier67/67 does not validate it.

Pending [panel cleanup regression](parity/world-callback-cleanup-2026-10-28.md):
Debug CRT heap snapshots check that World teardown releases allocated panel
buffers even when their visible count is zero. It runs before asset startup,
needs no window, and is compiled only; it has not been executed here. Release
does not run this Debug-only heap assertion.

The [native Options I/O check](parity/native-options-save-load-2026-10-28.md)
checks actual HWND input, live control callbacks, metadata/draw ordering and
OS Save/Load file I/O. The earlier pass stops at the post-save globe. The
[repaired isolated run](parity/win64-focus-timers-2026-10-28.md) also checks the
post-load globe and enabled Options callback. The focus-order checker rejects
the old nested gain/outer kill trace and accepts the repaired trace. Neither
check proves all UI screens or all loaded-state fields.

The [month-boundary flow](parity/month-boundary-live-2026-10-28.md) checks normal
hourly input, known-contact acknowledgement, report creation, close-control
wiring, budget application and fresh save4 loading. Upkeep is derived offline
from primitive inputs; funding-bar generation is not independently recomputed.

The [winning-save Home check](parity/victory-home-load-2026-10-28.md) checks
normal hourly recovery, one-time loot delivery and fresh save3 loading. Its
input craft is already home, so it does not prove a return flight.

The mission-spawn regression compares old and repaired code with the same test
fixture: eleven new target/index/RNG assertions fail on old code in Debug and
Release, and pass on the repair. It guards the reviewed subtype6 search and two
fallback branches; it does not prove the whole strategic game works.

