# Month report and save/load check

Status: this saved-game flow passes. Full game parity remains incomplete.

Run: [.workers/luna-month-boundary-continue/runs/month-boundary-20261028T073656Z-8c2b11cdd43a486b97f30bec5ade1827](../../.workers/luna-month-boundary-continue/runs/month-boundary-20261028T073656Z-8c2b11cdd43a486b97f30bec5ade1827).
The retained Release image has SHA-256
3561CB280ADB97D9FE049E19728231DB039A5E1E7B87022F36BFF89A2A3C7152.
The exact App source is retained as `provenance/actual-pufo_app.c`, SHA-256
5D13C4426187C8D529ADD94E35A763038064E0CCED9897F0D32583F1EA6CB863.
The earlier reviewed proposal also remains in provenance; it predates the
two-contact acknowledgement policy described below.

The fixture starts from the completed manufacture save3, SHA-256
AF6FA7CC87D717890731846B1C88BC59FC496B8388DCE61ABE33F3DC35922C7F.
Normal hourly input and 340 clock pulses reach screen19 on 1999/2/1 at00:12:48.
Two flag10 contacts occur at pulses186 and259. The fixture acknowledges each
through its installed live control and selects hourly speed again. Unknown
messages still fail the check. Raw instructions at0x818E8..0x818FC post flag10
and pause time; no IDA comment is used as behavior evidence. No clock, funds,
RNG or monthly-handler write is used to force the report.

The five observed bars are1625000,383000,1357000,1668000,968000: total6001000.
The offline check derives upkeep2860000 from primitive base inputs. Funds stay
4115500 before close and become7256500 after the real close control. The psi-lab
predicate selects return screen0. This verifies report application and upkeep;
it does not independently derive the upstream funding-bar generation formula.

Fresh-process loading of save4 preserves date, funds, staff, research, stock,
crew and cargo. Save4 SHA-256 is
2F9F87575FD6AFA58267A2C85EB904ACC8B6B6366901F9FC94C299E86516E173.
The fresh interval is5 seconds, from normal Geoscape initialization. All staged
input data, DLL and save3 hashes match after both processes exit.

Evidence hashes:

| File in run | SHA-256 |
| --- | --- |
| run-manifest.json | 37471612AF813C5AF49760C771D766FA88BC3C4C01FB17B424876102CFA97D4D |
| logs/month-boundary.log | B4597FD02F5E1AB66556FEEFE2296973D215A4C869D33ACC4BBD32668975DE78 |
| logs/fresh-load-slot4.log | 48CA483EF6FA3A1CB3F8A3E29F0D1174BE3F96AA7435118E99961E7707006839 |

The first staged runner failed on an extra Join-Path wrapper at line128; the
one-line fix is retained with its prior runner. The first actual game run
stopped safely on the first flag10 and wrote no save4. Both failed attempts
remain in the run folder tree. They were not counted as successful checks.
