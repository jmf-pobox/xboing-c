#ifndef SDL2_LOOP_H
#define SDL2_LOOP_H

/*
 * sdl2_loop.h — Fixed-timestep game loop timing module.
 *
 * Replaces the legacy XPending/usleep loop with a fixed-timestep accumulator.
 * Game logic runs at a fixed rate (determined by speed level), while rendering
 * runs as fast as possible (typically vsync-limited by the renderer).
 *
 * The module is time-source agnostic: callers pass elapsed milliseconds from
 * SDL_GetTicks64() or inject synthetic time for testing.  Internally, time
 * is tracked in microseconds for sub-millisecond precision at high speed
 * levels (e.g., 1.5ms tick interval at warp 9).
 *
 * Nine speed levels match legacy XBoing warp 1-9:
 *   tick_interval_us = 1500 * (10 - speed_level)
 *   Warp 1 (slowest): 13500 us = 13.5ms = ~74 ticks/sec
 *   Warp 5 (medium):   7500 us =  7.5ms = ~133 ticks/sec
 *   Warp 9 (fastest):  1500 us =  1.5ms = ~667 ticks/sec
 *
 * Opaque context pattern: no globals, fully testable.
 * See ADR-013 in docs/DESIGN.md for design rationale.
 */

#include <stdbool.h>
#include <stdint.h>

/* =========================================================================
 * Constants
 * ========================================================================= */

#define SDL2L_MIN_SPEED 1
#define SDL2L_MAX_SPEED 9
#define SDL2L_DEFAULT_SPEED 5

/* Tick interval base unit in microseconds.  Each speed level reduces
 * the interval by this amount from the maximum. */
#define SDL2L_TICK_UNIT_US 1500

/* Maximum logic ticks per update call — prevents spiral of death when
 * the game falls behind real time (e.g., breakpoint, suspend). */
#define SDL2L_MAX_TICKS_PER_UPDATE 10

/* =========================================================================
 * Status codes
 * ========================================================================= */

typedef enum
{
    SDL2L_OK = 0,
    SDL2L_ERR_NULL_ARG,
    SDL2L_ERR_INVALID_SPEED,
    SDL2L_ERR_ALLOC_FAILED
} sdl2_loop_status_t;

/* =========================================================================
 * Callback types
 * ========================================================================= */

/*
 * Logic tick callback.  Called zero or more times per update, once for each
 * fixed-timestep increment consumed from the accumulator.
 */
typedef void (*sdl2_loop_tick_fn)(void *user_data);

/*
 * Render callback.  Called exactly once per update, after all logic ticks.
 * alpha is the interpolation factor [0.0, 1.0) representing how far the
 * accumulator is between the last tick and the next.  Use this to
 * interpolate visual positions for smooth rendering.
 */
typedef void (*sdl2_loop_render_fn)(double alpha, void *user_data);

/* =========================================================================
 * Opaque context
 * ========================================================================= */

typedef struct sdl2_loop sdl2_loop_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/*
 * Create a game loop context with the given callbacks and user data.
 * tick_fn is called for each logic step; render_fn is called once per frame.
 * Either callback may be NULL (no-op).
 * Starts at SDL2L_DEFAULT_SPEED (warp 5), unpaused.
 *
 * Returns NULL on allocation failure.
 */
sdl2_loop_t *sdl2_loop_create(sdl2_loop_tick_fn tick_fn, sdl2_loop_render_fn render_fn,
                              void *user_data, sdl2_loop_status_t *status);

/* Destroy the game loop.  Safe to call with NULL. */
void sdl2_loop_destroy(sdl2_loop_t *ctx);

/* =========================================================================
 * Per-frame update
 * ========================================================================= */

/*
 * Advance the game loop by elapsed_ms milliseconds (from SDL_GetTicks64
 * delta or injected test time).
 *
 * Accumulates elapsed time, dispatches tick_fn for each fixed-timestep
 * interval consumed, then dispatches render_fn once with interpolation
 * alpha.  Returns the number of logic ticks dispatched (0 if paused).
 *
 * Clamps to SDL2L_MAX_TICKS_PER_UPDATE to prevent spiral of death.
 */
int sdl2_loop_update(sdl2_loop_t *ctx, uint64_t elapsed_ms);

/* =========================================================================
 * Speed control
 * ========================================================================= */

/* Set the speed level (1-9).  Returns error for out-of-range values. */
sdl2_loop_status_t sdl2_loop_set_speed(sdl2_loop_t *ctx, int level);

/* Get the current speed level (1-9). */
int sdl2_loop_get_speed(const sdl2_loop_t *ctx);

/* Get the tick interval for a speed level in microseconds. */
uint64_t sdl2_loop_tick_interval_us(int speed_level);

/* =========================================================================
 * Pause
 * ========================================================================= */

/* Set pause state.  When paused, update() returns 0 ticks and does not
 * call callbacks.  The accumulator is cleared on unpause to prevent
 * a burst of catch-up ticks. */
void sdl2_loop_set_paused(sdl2_loop_t *ctx, bool paused);

/* True if the loop is paused. */
bool sdl2_loop_is_paused(const sdl2_loop_t *ctx);

/* =========================================================================
 * Statistics
 * ========================================================================= */

/* Total logic ticks dispatched since creation. */
uint64_t sdl2_loop_total_ticks(const sdl2_loop_t *ctx);

/* Interpolation alpha from the last update call [0.0, 1.0). */
double sdl2_loop_alpha(const sdl2_loop_t *ctx);

/* =========================================================================
 * Utility
 * ========================================================================= */

/* Return a human-readable string for a status code. */
const char *sdl2_loop_status_string(sdl2_loop_status_t status);

#endif /* SDL2_LOOP_H */
