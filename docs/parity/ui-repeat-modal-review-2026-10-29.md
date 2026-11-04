# Geoscape repeat and modal-layout joins

The World repeat join omitted nine normal screen arms, and the pulse helper
reset its phase too early. The modal frame adapter also cleared layout values
that the original preserves. These source changes and focused regressions now
compile in full Debug and Release with zero diagnostics. No assertions or native
flows ran; see the [manifest](ui-repeat-modal-build-artifacts-2026-10-29.json).

## Held repeats

World_PulseGeoscape now supplies raw screen groups4,6,7,8,9,10,11,12,16,17,18
through live World control owners. The hangar order interleaves stores/crew;
research chooses its current pair; Ufopaedia processes sequential gated groups;
PSI uses the overlay-clear gate. The
[implementation report](../../.workers/luna-geoscape-repeat-join/RESULT.md) and
[peer review](../../.workers/luna-geoscape-repeat-peer/RESULT.md) record exact
counts, order and gates.
The final callback-write-set check confirms the clean Ufopaedia pair only scrolls
its category panel. It cannot change the wide flag before the next group; that
group's local inverted-wide snapshot is therefore stable in the reviewed pulse.

Raw0x95654 branches to0x95B80 after phase0 becomes1, bypassing the reset at
0x95B78. C previously reset unconditionally, so a normal zero-start pulse never
reached the repeat switch. It now carries1 to the next pulse, dispatches at2,
then resets. Saturation counters still run on both paths. See the
[parent byte check](geoscape-repeat-phase-parent-raw-2026-10-29.json).

The new World fixture presses a real Buy/Sell scroll control, checks the live
phase was cleared by that press, pumps two World pulses, checks one scroll step,
then releases and verifies later pulses do not repeat. The direct pulse test also
checks0->1->dispatch rather than forcing phase1. Old forced-phase tests isolated
the switch and could not detect the broken zero-start cadence.

## Preserved modal hit layout

Raw Begin/End leave layout slots untouched. A one-button draw updates X0/Y/count
but leaves X1 from the prior two-button draw. Input still checks both X ranges
when count is positive; hit2 ends the modal even when its callback is NULL.
World now seeds layout from its live input owner instead of zeroing the view.

The [World regression](../../.workers/luna-modal-layout-join/RESULT.md) draws two
buttons, ends that prepared modal, then draws one. It checks63/126 becomes95/126,
with count1, and uses the World pointer wrappers at the retained second region.
The original ghost hit is retained as a documented quirk. The fixture prepares
modal states and supplies an end hook; it does not execute the native quit or
inventory control sequence. This covers the World output join, unlike a local
UiModal_DrawDialog test that already had a preserved caller layout.

Native timer/hold/input acceptance remains open. The compiled fixtures do not
prove real OS message scheduling, audibility, or original-save interoperability.
