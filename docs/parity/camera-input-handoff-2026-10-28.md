# Loaded camera, selection, drawing and cell picking

Checked 2026-10-28 by Luna xhigh with parent raw capture. No source mismatch
was confirmed in this bounded handoff. No executable or pointer input was run.

The original pointer path reads camera scroll at0x14470..0x144A0 and uses
the isometric offset helpers before setting the map cursor. The draw path
uses the same camera fields at0x21E54/0x21E6C. The focus helper reads cell
coordinates at0x2C780/0x2C794/0x2C7E0, writes scroll at0x2C7BC/0x2C7D8,
then sets layer at0x2C7F0. Its navigation name mentioning a unit does not
establish the input type; the coordinate reads and caller data flow do.

Load restores camera/cursor words and roster selection, then mission init
calls focus at0x3D248 after the tactical-state read at0x3D234. The checked
port follows that recenter order and drawing/picking use live World focus,
cursor and Map selected-unit owners. Recenter after load is therefore part
of the original sequence, not evidence by itself of a lost camera field.

This does not prove native Win64 coordinates, DPI/window scaling, drag behavior,
every tile edge or every projection helper. Those need acceptance on the
authorized test host with the same frozen source and data.

[Raw handoff capture](camera-input-handoff-raw-2026-10-28.json).
[Luna's source map and limits](../../.workers/luna-camera-input/RESULT.md).
