/*
 * special_system.c — Pure C special/power-up state management.
 *
 * Owns 7 boolean special flags and their toggle logic.  The 8th special
 * (reverse) is owned by the paddle system and only appears in display
 * snapshots.  Side effects are communicated via injected callbacks.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-021 in docs/DESIGN.md for design rationale.
 */

#include "special_system.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal structure
 * ========================================================================= */

struct special_system
{
    int sticky_bat;
    int saving;
    int fast_gun;
    int no_walls;
    int killer;
    int x2_bonus;
    int x4_bonus;
    special_system_callbacks_t callbacks;
    void *user_data;
};

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

special_system_t *special_system_create(const special_system_callbacks_t *callbacks,
                                        void *user_data)
{
    special_system_t *ctx = calloc(1, sizeof(special_system_t));
    if (ctx == NULL)
    {
        return NULL;
    }

    ctx->user_data = user_data;

    if (callbacks)
    {
        ctx->callbacks = *callbacks;
    }

    /* calloc zeroes all fields — all specials start off */
    return ctx;
}

void special_system_destroy(special_system_t *ctx)
{
    free(ctx);
}

/* =========================================================================
 * Special toggles
 * ========================================================================= */

void special_system_set(special_system_t *ctx, special_id_t id, int active)
{
    if (ctx == NULL)
    {
        return;
    }

    int val = active ? 1 : 0;

    switch (id)
    {
        case SPECIAL_REVERSE:
            /* Owned by paddle system — reject */
            return;

        case SPECIAL_STICKY:
            ctx->sticky_bat = val;
            break;

        case SPECIAL_SAVING:
            ctx->saving = val;
            break;

        case SPECIAL_FAST_GUN:
            ctx->fast_gun = val;
            break;

        case SPECIAL_NO_WALLS:
            ctx->no_walls = val;
            if (ctx->callbacks.on_wall_state_changed)
            {
                ctx->callbacks.on_wall_state_changed(val, ctx->user_data);
            }
            break;

        case SPECIAL_KILLER:
            ctx->killer = val;
            break;

        case SPECIAL_X2_BONUS:
            ctx->x2_bonus = val;
            if (val)
            {
                /* x2 and x4 are mutually exclusive */
                ctx->x4_bonus = 0;
            }
            break;

        case SPECIAL_X4_BONUS:
            ctx->x4_bonus = val;
            if (val)
            {
                /* x4 and x2 are mutually exclusive */
                ctx->x2_bonus = 0;
            }
            break;
    }
}

void special_system_turn_off(special_system_t *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    /* Turn off all specials EXCEPT saving.
     * Matches legacy TurnSpecialsOff() where ToggleSaving is commented out. */
    int was_no_walls = ctx->no_walls;

    ctx->sticky_bat = 0;
    /* ctx->saving intentionally NOT cleared */
    ctx->fast_gun = 0;
    ctx->no_walls = 0;
    ctx->killer = 0;
    ctx->x2_bonus = 0;
    ctx->x4_bonus = 0;

    /* Fire wall state callback if walls were off */
    if (was_no_walls && ctx->callbacks.on_wall_state_changed)
    {
        ctx->callbacks.on_wall_state_changed(0, ctx->user_data);
    }
}

/* =========================================================================
 * Queries
 * ========================================================================= */

int special_system_is_active(const special_system_t *ctx, special_id_t id)
{
    if (ctx == NULL)
    {
        return 0;
    }

    switch (id)
    {
        case SPECIAL_REVERSE:
            return 0; /* Owned by paddle system */
        case SPECIAL_STICKY:
            return ctx->sticky_bat;
        case SPECIAL_SAVING:
            return ctx->saving;
        case SPECIAL_FAST_GUN:
            return ctx->fast_gun;
        case SPECIAL_NO_WALLS:
            return ctx->no_walls;
        case SPECIAL_KILLER:
            return ctx->killer;
        case SPECIAL_X2_BONUS:
            return ctx->x2_bonus;
        case SPECIAL_X4_BONUS:
            return ctx->x4_bonus;
        default:
            return 0;
    }
}

void special_system_get_state(const special_system_t *ctx, int reverse_on,
                              special_system_state_t *out)
{
    if (out == NULL)
    {
        return;
    }

    if (ctx == NULL)
    {
        memset(out, 0, sizeof(*out));
        return;
    }

    out->reverse_on = reverse_on ? 1 : 0;
    out->sticky_bat = ctx->sticky_bat;
    out->saving = ctx->saving;
    out->fast_gun = ctx->fast_gun;
    out->no_walls = ctx->no_walls;
    out->killer = ctx->killer;
    out->x2_bonus = ctx->x2_bonus;
    out->x4_bonus = ctx->x4_bonus;
}

/* =========================================================================
 * Panel label info
 * ========================================================================= */

/*
 * Legacy DrawSpecials() layout:
 *   Row 0: Reverse  Save    NoWall  x2
 *   Row 1: Sticky   FastGun Killer  x4
 *
 * Column X offsets: 5, 55, 110, 155
 */

void special_system_get_labels(const special_system_t *ctx, int reverse_on,
                               special_label_info_t *out)
{
    if (out == NULL)
    {
        return;
    }

    special_system_state_t state;
    special_system_get_state(ctx, reverse_on, &state);

    /* Row 0: Reverse, Save, NoWall, x2 */
    out[0].label = "Reverse";
    out[0].col_x = SPECIAL_COL0_X;
    out[0].row = 0;
    out[0].active = state.reverse_on;

    out[1].label = "Save";
    out[1].col_x = SPECIAL_COL1_X;
    out[1].row = 0;
    out[1].active = state.saving;

    out[2].label = "NoWall";
    out[2].col_x = SPECIAL_COL2_X;
    out[2].row = 0;
    out[2].active = state.no_walls;

    out[3].label = "x2";
    out[3].col_x = SPECIAL_COL3_X;
    out[3].row = 0;
    out[3].active = state.x2_bonus;

    /* Row 1: Sticky, FastGun, Killer, x4 */
    out[4].label = "Sticky";
    out[4].col_x = SPECIAL_COL0_X;
    out[4].row = 1;
    out[4].active = state.sticky_bat;

    out[5].label = "FastGun";
    out[5].col_x = SPECIAL_COL1_X;
    out[5].row = 1;
    out[5].active = state.fast_gun;

    out[6].label = "Killer";
    out[6].col_x = SPECIAL_COL2_X;
    out[6].row = 1;
    out[6].active = state.killer;

    out[7].label = "x4";
    out[7].col_x = SPECIAL_COL3_X;
    out[7].row = 1;
    out[7].active = state.x4_bonus;
}

/* =========================================================================
 * Attract mode
 * ========================================================================= */

special_system_state_t special_system_randomize(special_system_t *ctx, int (*rand_fn)(void))
{
    special_system_state_t state;
    memset(&state, 0, sizeof(state));

    if (ctx == NULL)
    {
        return state;
    }

    int (*get_rand)(void) = rand_fn ? rand_fn : rand;

    /* Each special gets 50/50 chance, matching legacy RandomDrawSpecials().
     * Legacy uses (rand() % 100) > 50, which gives 49% chance of True. */
    ctx->sticky_bat = (get_rand() % 100) > 50 ? 1 : 0;
    ctx->saving = (get_rand() % 100) > 50 ? 1 : 0;
    ctx->fast_gun = (get_rand() % 100) > 50 ? 1 : 0;
    ctx->no_walls = (get_rand() % 100) > 50 ? 1 : 0;
    ctx->killer = (get_rand() % 100) > 50 ? 1 : 0;
    ctx->x2_bonus = (get_rand() % 100) > 50 ? 1 : 0;
    ctx->x4_bonus = (get_rand() % 100) > 50 ? 1 : 0;

    /* Reverse is also randomized in attract mode (legacy line 246) */
    int reverse_val = (get_rand() % 100) > 50 ? 1 : 0;

    state.reverse_on = reverse_val;
    state.sticky_bat = ctx->sticky_bat;
    state.saving = ctx->saving;
    state.fast_gun = ctx->fast_gun;
    state.no_walls = ctx->no_walls;
    state.killer = ctx->killer;
    state.x2_bonus = ctx->x2_bonus;
    state.x4_bonus = ctx->x4_bonus;

    return state;
}
