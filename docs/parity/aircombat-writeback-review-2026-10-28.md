# Air-combat indices and live writeback

Checked 2026-10-28 by Luna xhigh against raw ARM and source. No pointer-index,
width or writeback defect was confirmed in this bounded path. No executable ran.

Original events store the craft-array index at radar block+4 at0x807B8 and
the strategic-list index at+8 at0x807C8. Target lookup/open uses that separate
list index at0x7FCA4. The World event env supplies a live radar session, and
frame refresh rewires its owners before radar update. The old comment saying
the radar env was absent/guarded off was false and is corrected.

Raw close-time initialization0x5FF88..0x60148 writes mount ammo to the live craft:
equal mounts sum both pools, distinct mounts write separately. Target damage
uses the list index and stores HP at StrategItem+0x6C at0x5DB94..0x5DBB0.
Range drawing mutates the selected live ship fuel at0x5D8E0..0x5D8FC.
Terminal close, stranded deletion and full-zoom exit keep craft index, list index
and stable craft ID distinct in their corresponding calls.

The full-zoom cleanup call at0x6143C is unreachable in the checked original
branches. The port preserves that behavior; this is not a missing port call.

Existing World fixtures drive events/draws and check target HP, close/ammo/pads,
while direct cases cover stranded loss/full zoom. They bypass native App timers
and do not assert a World-level fuel change. The source review is not a combat
playability pass. Native intercept, weapon hits, fuel use and all exits still
require acceptance on the same frozen build.
