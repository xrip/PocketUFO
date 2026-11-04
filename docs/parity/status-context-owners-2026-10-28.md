# Status pen scratch and Map state are separate owners

Checked 2026-10-28 by Luna xhigh from raw ARM and source. No status rendering
behavior mismatch was confirmed in this bounded handoff; no executable was run.

Render_Frame callers pass draw context0x10DA40. StatusMsg_DrawStack keeps it
in R4 and separately loads Map0x1139B8 into R5. Pen Y uses drawctx+0x2C at
0x1B000/0x1B048/0x1B054, so its address is0x10DA6C. Map+0x2C shot count is
0x1139E4. Equal offsets in different objects do not establish shared storage.

The status loop index is drawctx+0x100. Map+0x100 is a separate invv pad pointer.
Map+0xFC is the dpanelv pad: raw init stores it at0x3B3E8 and sets its Y origin
at0x3B8D4. The port callback uses that live pad's nOriginY. The false Map-based
pen/loop labels in the source header and World comment are corrected.

Original pen Y feeds the explicit Text_DrawString Y argument at0x1B0A0; the
text routine keeps R2 at0x2D7A0. Caller return values are overwritten. The
checked later draw paths reset pen scratch before reading it, including tile,
target-bar, status and minimap paths. The port uses local status Y/index and
explicit coordinates; no missing persistent pen output was established.

This does not prove every drawing primitive, visible pixel placement or native
HUD/window layout. Native tactical/status-frame acceptance remains open.
[Luna's base, consumer and HUD-source trace](../../.workers/luna-status-context/RESULT.md).
The parent backed up and updated the IDB note at0x1AFC4, checked readback and
saved with backup.

Ownership-comment corrections compile in both full configurations.
[Current source/image identities](byte-native-build-artifacts-2026-10-28.json).
No visible status-frame case was executed.
