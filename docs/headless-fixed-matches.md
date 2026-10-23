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

Host API **20** extends `MuMatchOverride` with an optional initial spawn-point
flag and six stage-defined indices. Append `spawns A B` to `--match` to select
two distinct indices in 0..3 for the configured logical players; `ports` and
`spawns` may appear in either order, once each. The callback table stays unchanged.
For the example above, `spawns 0 1` places P2/P3 at Yoshi's left/right starting
platforms. The ordinary game constructor consumes `StartMeleeData.spawn_pos`;
this does not enable neutral-spawn codes or recover the source game's settings.
No live coordinates, update logic, timing or math are changed. Omitting this
option preserves ordinary per-slot starts and the previous recording bytes.
Rebuild host and game together after the API change.

Host API **21** appends optional `game_start_rng_seed_override`. The CLI's
`--rng-seed-boundary slippi-game-start` requires `--rng-seed` and applies it at
the existing native RestoreGameInfo boundary, before genuine Game Start recording.
The later item-setup callback then returns absent. Default `item-setup` behavior
is unchanged; playback/online seed handling is untouched. The recorded seed comes
from the actual game RNG, without editing encoded metadata. Docker unit/host
checks cover callback exclusivity and old-version/null guards; an ISO search
records the supplied 33480 and reproduces fresh observations/keys/replay bytes.
The prior default two-jump replay remains byte-identical. This aligns an initial
seed boundary, not a source replay's full code set or subsequent hidden state.
Both accepted branches of the unchanged 64-program supplied-example grid also
pass actual official Playback and nine-field comparison over 1..17 (68
player-frames total), with resync disabled and requested/recorded seed 33480.

The first real-footage opening search used all 72 declared two-controller
programs at replay checkpoint -40. Ordinary slot starts rejected all 72. With
the source's visibly left/right starting platforms declared as indices 0/1,
24 pass sparse damage/order cues through frame 18. The selected genuine partial
recording reproduces from a fresh boot; independent parsing and actual official
Playback pass. Nine state fields match exactly across -39..18 (116 player-frames),
and seven native observation fields match their signed recording keys. This
establishes engine/playback consistency for the tested branch, not video likeness,
original inputs or complete pre-clock parity. The separate Ready failure below
remains unresolved.

The cooperative controller and checkpoint gate recognize exactly two live
fighters, including non-default slots. All four PAD samples still pass through
ordinary polling. Callers must connect the active ports and disconnect unused
ones explicitly. Initial control waits for a positive match counter by default.
`--control-preroll` opts into a pause as soon as both fighters exist, while the
native match counter is zero. The independent signed recording status begins at
-123 in the measured case and advances one per VI. Early capture/restore uses the
same session-local process checkpoint; no game ABI/timing/math change is involved.
The flag requires `--control-fd`.

A genuine pre-clock probe captures at replay -40 and controls both fighters before
frame 0. Three restores and a fresh boot reproduce observations and bytes. Official
Playback is nine-field exact over the requested playable interval -39..140 (360
player-frames). A strict wider audit -123..140 retains one Falco Y word mismatch
at -113; all other compared words match. Full Ready-state equality is not claimed,
and no tolerance or math change was introduced. Native observations match their
seven recorded fields over the entire interval using observed signed counters.

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
