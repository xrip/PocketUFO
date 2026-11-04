# Winning-save Home and cold-load check

The retained winning save passes fresh loading, seven normal hourly checks,
loot delivery and a fresh slot-three load. This closes this saved-state flow,
not a full return flight: the craft was already at home in the input.

The final run is [.workers/luna-victory-home-continue/runs/run_20261028T100816Z_4124](../../.workers/luna-victory-home-continue/runs/run_20261028T100816Z_4124).
Its [manifest](../../.workers/luna-victory-home-continue/runs/run_20261028T100816Z_4124/manifest.json)
has SHA-256 FCC28EDBE1C27151499AA51A342465276CC5202AAEA8C4E7837456DCE9D23CE1.
The [fresh-load log](../../.workers/luna-victory-home-continue/runs/run_20261028T100816Z_4124/phase3.log)
has SHA-256 F92363F8E38036A6C0BA93A4A7749F5AB72350A73ECE85F41C294900A583D998.

The checked slot-three save is
51E699CAF18A92299126A1F120ACE5D65A9D5CB79C2371F421AE3D7013C94EE4.
Cold loading retains saved clock/date/pause, health, ammo, stocks and cleared
site/cargo. Item105 stock is43. The fresh interval is5 seconds, as set by normal
Geoscape init; the chosen hourly speed is not serialized. No save4 was written.
The full checkpoint was taken immediately before save3; the post-save record
is a summary. No hours or deliveries were replayed by the final load check.

This run uses the retained Release image
4D2004F17B8A8489AE8A1DF9D2DA1079D3DAE49725E6FD0BC9B4682409A0EE13.
It does not prove the later mission/month App build or a natural return flight.
