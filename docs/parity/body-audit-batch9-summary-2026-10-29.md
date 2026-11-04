# Battle end, research and text body review

15 new complete raw body captures match the original PE mapped bytes exactly.
Original EXE SHA256:
`4332B3373FDFF386003C9B097C05A616F9E7BB4E085DDCB19474A97A3A36B8E1`.
The index has 223 unique bodies after the separate fall-action addition below.
This is evidence coverage, not game completion.

- Battle end: 2 bodies. Outcome/restart paths reviewed; Mars re-init is deferred
  by World until the active tick ends. A static path leaves a button modal open
  into debrief fade. Color/fade equivalence is still under review.
- Research: 8 bodies. Default item gate really returns zero at raw 0x72D00.
  The original topic47 unlock range can exceed the 144-entry topic table; the
  port bounds it. This is an explicit safety adaptation, not full equivalence.
- Text: 5 bodies. Raw advance loads are signed. The earlier unsigned C reads
  are repaired in both width and draw. Shipped advance bytes are0..14, so
  existing shipped spacing is unchanged. Exhaustive return-use review of the
  original Text_DrawString callers remains open.

Reports and raw capture hashes are in `body-audit-index-2026-10-29.json`.
They describe baseline source; its text cluster has an explicit current override.
The signed-spacing regression and the separate manufacturing/fall connection
repairs compile in the current `connected-fixes-build-2026-10-29.json` candidate.
No tests or native flows ran. No source or IDB change was made by this indexing pass.

The separate `fall-action` report adds one 5,716-byte body at0x52ADC. Its span
also matches the original EXE. All1,429 word slots are classified:1,363 code
heads,50 MOV/ORR macro tails,16 data words. The roster count repair is compiled
in6401F9FA; the two-call regression remains unrun and does not prove the full
World/GameTick/EnemyAI path.
