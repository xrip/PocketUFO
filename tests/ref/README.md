# Frame references

Binary PPM frames the tests dump while they run (see `DumpFrame` in `tests/test_geoscapeframe.c`).
`sh tools/frames.sh` rebuilds them and reports any change: a clean `git diff tests/ref` means the
draws still paint exactly the pictures recorded here.

- `tact_frame_iso.ppm` -- `World_TactFrame` over the live mission: the iso layer plus the frame furniture.
- `tact_loaded_mission.ppm` -- the same frame over the mission a slot file brought back (`World_LoadSlot` + the driver's `Tact_InitMission`).
- `debrief_banner.ppm` -- screen 13, `TactHud_DrawStatusBanner @ 0x1e920`: backdrop, caption, footer, score.
