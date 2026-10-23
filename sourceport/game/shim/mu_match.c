/* The match a scripted run asked for.
 *
 * The sweep has to play every character on every stage. Driving the character
 * and stage screens by cursor position for each combination is slow and
 * fragile: one mistimed press and the run is testing the wrong thing without
 * saying so. The host takes --match instead and the game asks for it once, at
 * the point where a VS match's rules and players are finalised
 * (gmvsmelee.c, gmVsMelee_EnterVs). The menus still run exactly as they do for
 * a player; only the outcome is forced.
 *
 * Game code never includes mu_shim.h, so gmvsmelee.c declares this as plain
 * ints and the ABI stays on this side of the line.
 */
#include "mu_host.h"
#include "mu_shim.h"

int mu_match_request(int* stage, int* kind, int* cpu, int* level, int* costume,
                     int count)
{
    const MuMatchOverride* over;
    int i;

    if (mu_host->version < 2 || mu_host->match_override == NULL) {
        return 0;   /* an older host: nothing to force */
    }
    over = mu_host->match_override();
    if (over == NULL) {
        return 0;   /* the ordinary case: no --match on the command line */
    }

    *stage = over->stage;
    for (i = 0; i < count; i++) {
        if (i >= (int) (sizeof over->players / sizeof over->players[0])) {
            kind[i] = -1;
            continue;
        }
        kind[i] = over->players[i].kind;
        cpu[i] = over->players[i].cpu;
        level[i] = over->players[i].cpu_level;
        costume[i] = over->players[i].costume;
    }
    return 1;
}

/* Optional ordinary stock rules, applied before constructing the VS match.
 * Older hosts and hosts without an explicit request leave menu defaults alone. */
int mu_stock_rules_request(int* seconds, int* stocks)
{
    int32_t requested_seconds, requested_stocks;
    if (mu_host->version < 19 || mu_host->stock_rules_override == NULL ||
        !mu_host->stock_rules_override(&requested_seconds, &requested_stocks)) {
        return 0;
    }
    if (requested_seconds < 60 || requested_seconds > 600 || requested_seconds % 60 ||
        requested_stocks < 1 || requested_stocks > 4) {
        return 0;
    }
    *seconds = requested_seconds;
    *stocks = requested_stocks;
    return 1;
}

/* Stage-defined start positions are ordinary game setup fields. No coordinates,
 * fighter state or physics are rewritten once the match has started. */
int mu_spawn_request(int* points, int count)
{
    const MuMatchOverride* over;
    int i;
    if (mu_host->version < 20 || mu_host->match_override == NULL || count < 1 || count > 6) {
        return 0;
    }
    over = mu_host->match_override();
    if (over == NULL || !over->has_spawn_points) return 0;
    for (i = 0; i < 6; i++) {
        if (over->players[i].kind >= 0 && (over->spawn_points[i] < 0 || over->spawn_points[i] > 3)) return 0;
    }
    for (i = 0; i < count; i++) points[i] = over->spawn_points[i];
    return 1;
}

/* --rng-seed (M7 lockstep parity): the seed is set at the first in-match setup RNG boundary,
 * immediately before it_8026D018 draws the ambient item-spawn delay. The previous first-frame
 * hook ran after this gameplay-affecting draw, leaving it uncontrolled. Mark the scripted input
 * origin separately at the first GS_VS frame so match-relative inputs keep their established
 * meaning. A pre-version-3 host leaves both callbacks NULL.
 *
 * The actual write through HSD_RandSeedPtr happens in mu_apply_seed (mu_snapshot.c): that file
 * already includes the game's random.h, and random.h drags in Runtime/platform.h, whose ssize_t
 * conflicts with the one mu_host.h gets from the Windows SDK's stdint.h when both land in the same
 * translation unit (the same reason mu_entry.c keeps game headers out of itself). This function
 * stays the ABI side, with no game headers, exactly like mu_match_request above it. */
void mu_apply_seed(uint32_t seed);

void mu_rng_seed_request(void)
{
    if (mu_host->version >= 3 && mu_host->rng_seed_override != NULL) {
        uint32_t seed = 0;
        int32_t has_value = 0;
        mu_host->rng_seed_override(&seed, &has_value);
        if (has_value) {
            mu_apply_seed(seed);
            if (mu_host->log) mu_host->log("rng-seed: forced (VS item-setup boundary)");
        }
    }
}

void mu_seed_request(void)
{
    if (mu_host->version >= 3 && mu_host->mark_match_start != NULL) {
        mu_host->mark_match_start();
    }
}

void mu_game_start_seed_request(void)
{
    if (mu_host->version >= 21 && mu_host->game_start_rng_seed_override != NULL) {
        uint32_t seed = 0;
        int32_t has_value = 0;
        mu_host->game_start_rng_seed_override(&seed, &has_value);
        if (has_value) {
            mu_apply_seed(seed);
            if (mu_host->log) mu_host->log("rng-seed: forced (Slippi Game Start boundary)");
        }
    }
}
