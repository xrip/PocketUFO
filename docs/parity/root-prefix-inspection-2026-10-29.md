# Root line-list prefix inspection

Read-only inspection confirms address0x158890 is used as the inline line-list
base, with its count at+0xD48. The count has a four-byte data definition at
0x1595D8; the next named pointer is pFundPad at0x1595DC, matching the declared
view's3404-byte span. This supports the known prefix, not a complete root-owner
layout or a7092-byte global-object claim.

The stored global type is an unresolved #109 reference with BADSIZE and a
one-byte item definition. The existing named UiPanel type is a separate7092-byte
type. No global type, data definition, label or neighboring pointer was changed.

[Captured annotation/definitions](root-prefix-annotation-before-2026-10-29.json)
include serialized type bytes for later recovery. The applied function-parameter
UiLineListView remains valid for its observed offsets; the stale global
reference stays an explicit open item.
