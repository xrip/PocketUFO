# Natural fighter-to-radar runtime

Date: 2026-10-26

Status: **PROVED through natural radar combat and a landed-site result.**
Crew-transport ground entry, tactical entry, and campaign parity remain open.

## Preserved run

The saved [run note](../../.workers/luna-parent/normal-air-radar-boundary-20261026/README.md)
pins the executable, flow, and capture. The app log is
[normal_air.log](../../.workers/luna-parent/normal-air-radar-boundary-20261026/normal_air.log);
the [checker-only result](../../.workers/luna-runtime-gate-v2/20261026T142004443Z-afedb352/normal-air-saved-checker-20261026T142004443Z-afedb352.log)
passes with exit 0 against that saved log. The earlier checker expected a
`target=` field where a detailed snapshot uses `index=`. The saved run was not
replayed to fix this checker mismatch.

The run starts from the real New Game and base-placement controls. A live
five-minute speed click runs 1868 C353 pulses (301 seconds each); the clock goes
from 0 to 562268 seconds, and visible contact index 5 / seed 18 is found. Its
saved state is active, visible, type 1, subtype 0, phase 3, timer 0. No contact,
visibility, clock, craft, weapon, or outcome field is forced by the helper.

The real Base Options animation takes four frames without advancing the clock.
The real Options row selects craft 1, type 3, with weapons 25/26, ammo 10/3,
fuel 100, range 1000, and flag 27. The UFO map click changes flag 27 to 10;
geo1 confirm launches that craft to target 5. The app log records at-base
1→0, intercept 0→1, target 0→5, and `PASS stage=real_order`.

The live one-minute control then runs 57 C353 pulses at 61 seconds each. At
clock 565745, radar is active and the clock is paused. There is no modal or
message; target 5 / seed 18 remains active and visible with timer 0, and craft 1
remains assigned to it with fuel 100 and range 772. Radar initialization changes
target HP from `-1` to `250`. The app records `PASS stage=radar_boundary` without
a crash. The target's `bCanDogfight` field is logged as 0; that value does not
establish a capability gate.

The executable, flow, and log hashes are recorded in the saved README. The app
hash is
`2349BD1616E0720C84FC2D78E382CE60078BB3BA66DDFC2CBEA3807183297D2D`.

## Real radar combat and landed site

The separate private [combat capture](../../build/e2e_normal_air_combat/run_20261026T151625Z_1176/normal_air_combat.log)
continues from the same natural contact and real fighter order. Through the
live NE/SW range controls and normal paint/C353 flow, it records right ammo
3→0, left ammo 10→9, and target/item HP 250→165→115. The target remains index
5 / seed 18 throughout. The terminal observer records target type 4, active and
visible at 388/55, `nTimer=7200`, session done=1, and the landed-site message
record. That timer is a stored event-pass counter here; do not report it as
seconds. The radar then closes through its normal cleanup: radar active 1→0,
pause 1→0, intercept 1→0, and ammo writes back as left 9 / right 0.

The [checker-only result](../../.workers/luna-runtime-gate-v2/20261026T162340508Z-1334db99/normal-air-combat-checker-20261026T162340508Z-1334db99.log)
passes on the saved capture. The first checker rejected the valid record
because it parsed the HP current/max fields with the wrong schema; the corrected
checker did not replay the app. The parent reviewed the five captured PPMs in
[the image sheet](../../.workers/luna-parent/review/normal-air-combat-20261026.png).
These show the radar controls, projectiles, damage gauge, landed notice, and
cleanup; they are not a comparison with original WinCE screenshots. No model
field was forced by the flow.

## Ground-entry attempt and limit

The follow-on [normal-ground capture](../../build/e2e_normal_ground/run_20261026T190106Z_260/normal_ground.log)
observes the same active type-4 site at 388/55 with timer 7200 and HP 115. It
opens the base Options screen, then stops when its roster helper sees a repeated
strategic soldier ID `0` in the assigned-crew list. The raw identity meaning is
under review; this is a helper stop, not a confirmed game defect. The run does
not reach a site order, flag 20/23, `Tact_InitMission`, or tactical entry. Its
arrival checker can also match later ground-map clicks; click checks need phase
scope before they count as evidence.

## Original ARM path for this boundary

The addresses below are from the linked raw ARM review. IDA comments are not
used as proof.

- `UiPulse_OnTimer @ 0x36630` calls `Geoscape_PulseUpdate` at `0x3664C`. The
  pulse path checks the geoscape UI byte; `Geoscape_PulseUpdate` checks the
  pause byte at `0x95610..0x95618` before advancing game time.
- `Geoscape_AdvanceTime @ 0x5CE40` runs minute ticks through
  `Geoscape_TickShips @ 0x5D4E4`. That routine calls `StrategList_UpdateMotion`
  and the live event pass. The motion update and event processing therefore
  stay on the normal clock path.
- `StrategList_ProcessEvents @ 0x7FCB0` moves the own marker toward its target.
  Arrival needs both coordinate deltas below 2 at `0x80348..0x80368`. The
  arrival code then reads the type-1 target timer at `0x80EE4..0x80EE8`.
- A target timer `<= 0` calls `Strateg_ConfirmInterceptTarget @ 0x7FB98`; its
  nonpositive branch calls `RadarScreen_SetActive(1)` at `0x7FC9C..0x7FCA4`.
  `RadarScreen_SetActive @ 0x6144C` initializes radar and sets the game pause
  state at `0x61464/0x61480`. This matches the observed radar-open, paused state.
- A positive target timer takes the separate flag-23 ground-offer branch at
  `0x80EF4..0x80F08`. The initial radar-boundary run used subtype 0 / phase 3 /
  timer 0; the later radar-combat run naturally changed that same target to
  type 4 with stored timer 7200. Neither run proves the crew-transport order or
  tactical entry.

The raw radar hit path applies `RadarScreen_RollWeaponHit @ 0x5DB24` during
`RadarScreen_DrawOverlay @ 0x5DC84` and writes the result to session and target
HP at `0x5DB90`/`0x5DBB0`. The run ends at 115/250 HP (46%). The raw
40–69-percent branch calls `RadarScreen_RollDetection(2)` and its trace/tile/
random gates (`0x60A28..0x60AA8`); its landed-site arm writes the existing
target's type 4, timer 7200, and intercept-done state at `0x60B30..0x60B4C`.
The separate below-40-percent branch at `0x60B70..0x60C74` sets the crash
message and checks the tile. Do not describe the observed 46% result as that
below-40 branch. The normal close path runs once the message timer permits it
and writes interceptor ammo back from the session (`0x5FF88..0x60144`). These
are raw ARM locations from the
[combat/ground proposal](../../.workers/luna-normal-intercept-impl/normal-air-combat-ground-proposal.md),
not behavior inferred from IDA comments. The value 7200 is recorded as the
target's timer; its unit is not assumed to be seconds.

The raw travel, target-timer, radar-control, and ground-entry review is in the
[arrival proposal](../../.workers/luna-normal-intercept-impl/normal-arrival-proposal.md)
and [air-combat/ground-entry proposal](../../.workers/luna-normal-intercept-impl/normal-air-combat-ground-proposal.md).
`Ship_LaunchIntercept @ 0x5A64C` has no weapon, ammo, or `bCanDogfight` gate in
that raw path.

## Open stages

The radar-combat run proves firing, target damage, ammo writeback, a landed-site
result, and radar cleanup. It does not prove a crew-transport order to that
site, flag 23, `Tact_InitMission`, or a mission return. The current ground-entry
attempt stops at its duplicate-ID helper guard before any site click. The next
runtime proof must resolve the owner/index observation, scope click checks to
that stage, and then verify the raw flag-20/23 and tactical-init boundaries.
