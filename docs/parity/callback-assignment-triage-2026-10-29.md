# Callback assignment triage

A static source scan found 219 distinct called pfn names and 290 distinct names
with an explicit field assignment. Eighteen called names had no such assignment.
This is navigation evidence, not a complete binding proof: field names can be
shared by different structs, and tables can initialize fields positionally.

The eighteen candidates were accounted for:

- Fifteen audio backend fields use positional PufoAudioBackend tables. App installs
  g_pufoAudioWinmm; the bounded audio wrapper audit records the external-output limits.
- ShipSupplyHooks.pfnLowSupplyMsg is supplied positionally by Geoscape_TickHourly
  as Supply_LowMsg, alongside its SupplyUser. This is not a missing callback.
- The generic tactical shutdown helper's optional panel/control cleanup callbacks
  have no production named assignment. The World outcome path and World_FreeTactical
  perform the actual owned cleanup. The scan does not establish full cleanup parity.

No new fault was confirmed from this filter. Existing placeholder triage and
raw connection audits remain necessary for conditional/partial handlers.
No source changes, tests, game, decoder, audio/device or debugger ran in this scan.

The slot-list App owner fault was found separately by raw driver review: having
a callback installed does not prove its context points to the right live state.
