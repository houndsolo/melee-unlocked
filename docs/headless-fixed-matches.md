# Fixed matches in the Linux headless host

The offline control host accepts Yoshi's Story (external stage 8), Battlefield
(31) and Final Destination (32), two retail characters, and two distinct
zero-based ports. Unspecified ports are 0/1. The existing nine-integer match
argument is unchanged; append `ports A B` for a different pair:

```bash
melee_headless --iso /assets/melee.iso --game-library melee_game.so \
  --headless --fast --vanilla-game --no-gc-adapter \
  --input-script native_vs.txt --frames 3000 \
  --match '8 20 0 0 9 0 0 0 9 ports 1 2' --stock-rules '480 4'
```

This requests Falco and Captain Falcon on physical P2/P3 with four stocks and an
eight-minute countdown. `--stock-rules` requires an explicit match. Seconds must
be whole minutes in 60..600; stocks must be 1..4. When omitted, menu rules remain
unchanged. The callback changes match kind, stock flag, countdown settings and
active players' initial stock counts during setup. Item, team and other rules
retain defaults. No update loop, timing constant, game speed or physics math is
changed.

Host API version 19 appends optional `stock_rules_override` after all existing
callbacks. `mu_stock_rules_request` checks version/pointer/bounds before returning
values to `gmVsMelee_EnterVs`. The game patch applies them before normal match
construction. Rebuild both host and game library; the entry point requires
matching API versions. Existing game structures, state POD layout and control
wire protocol are unchanged.

The cooperative controller and checkpoint gate recognize exactly two live
fighters, including non-default slots. All four PAD samples still pass through
ordinary polling. Callers must connect the active ports and disconnect unused
ones explicitly. Initial control still waits for a positive match counter;
this does not cover playable pre-clock replay frames.

Validation in YT2SLP's GCC 14 Docker environment: 10 host/component tests and
23 game-library tests passed. A local retail-ISO probe captured at 241, restored
three times and ran continuations to 421 with controls on both P2 and P3. Matching
branches and a fresh boot reproduce observations/recording bytes; the neutral
branch differs for both fighters and does not leak into the selected recording.
Pinned slippi-js parsing confirms stage, characters, physical ports, four stocks
and 480-second countdown. Actual official Playback 3.5.2 with resync disabled
matches nine fields across 842 player-frames (1..421). This is selected-field
evidence, not complete-state or retail parity. Default settings still produce
the prior two-jump benchmark's exact replay bytes.
