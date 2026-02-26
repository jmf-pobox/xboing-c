#ifndef SPECIAL_SYSTEM_H
#define SPECIAL_SYSTEM_H

/*
 * special_system.h — Pure C special/power-up state management.
 *
 * Owns 7 boolean special flags (sticky, saving, fastGun, noWalls, killer,
 * x2, x4) and their display state.  The 8th special (reverse) is owned by
 * the paddle system but included in the display snapshot for rendering.
 *
 * Side effects (border color change, panel redraw) are communicated via
 * an injected callback table.  Zero dependency on SDL2 or X11.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-021 in docs/DESIGN.md for design rationale.
 */

/* =========================================================================
 * Constants — match legacy special.h values
 * ========================================================================= */

#define SPECIAL_COUNT 8   /* Total specials displayed in panel */
#define SPECIAL_FLASH 500 /* Frame interval for attract-mode randomization */

/* Panel layout constants (pixels, for integration layer rendering) */
#define SPECIAL_PANEL_W 180
#define SPECIAL_PANEL_H 35
#define SPECIAL_COL0_X 5
#define SPECIAL_COL1_X 55
#define SPECIAL_COL2_X 110
#define SPECIAL_COL3_X 155
#define SPECIAL_ROW0_Y 3
#define SPECIAL_GAP 5 /* Vertical gap between rows */

/* =========================================================================
 * Special identifiers
 * ========================================================================= */

typedef enum
{
    SPECIAL_REVERSE = 0, /* Owned by paddle system, included for display */
    SPECIAL_STICKY,
    SPECIAL_SAVING,
    SPECIAL_FAST_GUN,
    SPECIAL_NO_WALLS,
    SPECIAL_KILLER,
    SPECIAL_X2_BONUS,
    SPECIAL_X4_BONUS,
} special_id_t;

/* =========================================================================
 * Read-only state snapshot — for rendering the panel
 * ========================================================================= */

typedef struct
{
    int reverse_on; /* Injected by caller, owned by paddle system */
    int sticky_bat;
    int saving;
    int fast_gun;
    int no_walls;
    int killer;
    int x2_bonus;
    int x4_bonus;
} special_system_state_t;

/* Panel label info for a single special */
typedef struct
{
    const char *label; /* Display text (e.g., "Reverse", "x2") */
    int col_x;         /* X pixel offset in panel */
    int row;           /* 0 = top row, 1 = bottom row */
    int active;        /* 1 = active (yellow), 0 = inactive (white) */
} special_label_info_t;

/* =========================================================================
 * Callback table — injected at creation time
 * ========================================================================= */

typedef struct
{
    /*
     * Fired when no_walls changes state.
     * Integration layer updates play area border color.
     */
    void (*on_wall_state_changed)(int no_walls, void *ud);
} special_system_callbacks_t;

/* =========================================================================
 * Opaque context
 * ========================================================================= */

typedef struct special_system special_system_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/*
 * Create a special system context.
 *
 * callbacks: callback table (copied, caller retains ownership).
 *            NULL is safe — wall state changes are silently ignored.
 * user_data: opaque pointer passed to all callbacks.
 *
 * Initial state: all specials off.
 * Returns NULL on allocation failure.
 */
special_system_t *special_system_create(const special_system_callbacks_t *callbacks,
                                        void *user_data);

/* Destroy the special system.  Safe to call with NULL. */
void special_system_destroy(special_system_t *ctx);

/* =========================================================================
 * Special toggles
 * ========================================================================= */

/* Set an individual special on or off.
 * For SPECIAL_NO_WALLS, fires on_wall_state_changed callback.
 * For SPECIAL_X2_BONUS, deactivates x4 (mutually exclusive).
 * For SPECIAL_X4_BONUS, deactivates x2 (mutually exclusive).
 * SPECIAL_REVERSE is rejected (owned by paddle system). */
void special_system_set(special_system_t *ctx, special_id_t id, int active);

/* Turn off all specials EXCEPT saving.
 * Matches legacy TurnSpecialsOff() behavior — saving persists
 * until explicitly cleared after a successful save operation. */
void special_system_turn_off(special_system_t *ctx);

/* =========================================================================
 * Queries
 * ========================================================================= */

/* Return whether a specific special is active.
 * Returns 0 for SPECIAL_REVERSE (use paddle system). */
int special_system_is_active(const special_system_t *ctx, special_id_t id);

/*
 * Fill a state snapshot for rendering.
 *
 * reverse_on: current reverse state from the paddle system.
 * out:        populated with all 8 special states.
 */
void special_system_get_state(const special_system_t *ctx, int reverse_on,
                              special_system_state_t *out);

/*
 * Get panel label info for all 8 specials.
 *
 * reverse_on:  current reverse state from the paddle system.
 * out:         array of SPECIAL_COUNT elements, filled with label info.
 *
 * Layout matches legacy DrawSpecials():
 *   Row 0: Reverse(0), Save(1), NoWall(2), x2(3)
 *   Row 1: Sticky(0),  FastGun(1), Killer(2), x4(3)
 */
void special_system_get_labels(const special_system_t *ctx, int reverse_on,
                               special_label_info_t *out);

/* =========================================================================
 * Attract mode
 * ========================================================================= */

/*
 * Randomize all special states for attract-mode display.
 *
 * Each special gets a 50/50 chance of being active.
 * Returns the randomized state snapshot (includes reverse_on).
 * The caller provides a random seed source via the rand_fn callback.
 *
 * rand_fn: returns a random int (e.g., wraps rand()).
 *          If NULL, uses stdlib rand().
 */
special_system_state_t special_system_randomize(special_system_t *ctx, int (*rand_fn)(void));

#endif /* SPECIAL_SYSTEM_H */
