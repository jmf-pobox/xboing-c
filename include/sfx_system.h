#ifndef SFX_SYSTEM_H
#define SFX_SYSTEM_H

/*
 * sfx_system.h — Pure C visual special effects state machine.
 *
 * Owns 5 effect modes (SHAKE, FADE, BLIND, SHATTER, STATIC), an
 * enable/disable toggle, and the BorderGlow / FadeAwayArea utilities.
 *
 * All rendering is delegated to the integration layer via callbacks
 * and query functions.  The module computes coordinates, timing, and
 * tile order; the integration layer performs the actual draw calls.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-023 in docs/DESIGN.md for design rationale.
 */

/* =========================================================================
 * Constants — match legacy sfx.c values
 * ========================================================================= */

#define SFX_SHAKE_DELAY 5      /* Minimum frame interval between shake moves */
#define SFX_NUM_SCAT 10        /* Scatter grid dimension (10x10 tiles) */
#define SFX_FADE_STRIDE 12     /* Pixel stride for fade grid lines */
#define SFX_FADE_STEPS 13      /* Total frames for fade effect (0..12) */
#define SFX_STATIC_DURATION 50 /* Frames for static effect (placeholder) */

/* Canonical play window position (legacy hardcoded values) */
#define SFX_WINDOW_X 35
#define SFX_WINDOW_Y 60

/* BorderGlow constants */
#define SFX_GLOW_FRAME_INTERVAL 40 /* Frames between glow color changes */
#define SFX_GLOW_STEPS 7           /* Number of color steps per cycle */

/* FadeAwayArea constants */
#define SFX_FADEAWAY_STRIDE 15 /* Pixel stride for fade-away grid */

/* Devil eyes constants — match legacy stage.c values */
#define SFX_DEVEYE_WIDTH 57
#define SFX_DEVEYE_HEIGHT 16
#define SFX_DEVEYE_FRAMES 6   /* Number of sprite frames */
#define SFX_DEVEYE_SEQ_LEN 26 /* Length of blink sequence */
#define SFX_DEVEYE_MARGIN 5   /* Margin from play area edge */

/* =========================================================================
 * Effect mode enum — match legacy sfx.h defines
 * ========================================================================= */

typedef enum
{
    SFX_MODE_NONE = 0,
    SFX_MODE_SHAKE = 1,
    SFX_MODE_FADE = 2,
    SFX_MODE_BLIND = 3,
    SFX_MODE_SHATTER = 4,
    SFX_MODE_STATIC = 5,
} sfx_mode_t;

/* =========================================================================
 * Shake output — position offset per frame
 * ========================================================================= */

typedef struct
{
    int x; /* Window X position (canonical + random offset) */
    int y; /* Window Y position (canonical + random offset) */
} sfx_shake_pos_t;

/* =========================================================================
 * Fade output — line drawing commands for one frame
 * ========================================================================= */

typedef struct
{
    int step;   /* Current step index (0..12) */
    int stride; /* Pixel stride (SFX_FADE_STRIDE) */
    int w;      /* Target area width */
    int h;      /* Target area height */
} sfx_fade_frame_t;

/* =========================================================================
 * Shatter tile — one copy-area command in the scatter sequence
 * ========================================================================= */

typedef struct
{
    int x; /* Source and destination X */
    int y; /* Source and destination Y */
    int w; /* Tile width */
    int h; /* Tile height */
} sfx_shatter_tile_t;

/* =========================================================================
 * Blind strip — one copy-area command in the blind reveal
 * ========================================================================= */

typedef struct
{
    int x; /* Column X position */
    int h; /* Strip height (full play height) */
} sfx_blind_strip_t;

/* =========================================================================
 * BorderGlow state — color index and direction for ambient glow
 * ========================================================================= */

typedef struct
{
    int color_index; /* Current color step (0..SFX_GLOW_STEPS-1) */
    int use_green;   /* 0 = red phase, 1 = green phase */
} sfx_glow_state_t;

/* =========================================================================
 * Devil eyes render info — current animation frame and position
 * ========================================================================= */

typedef struct
{
    int frame_index; /* Sprite frame index (0..SFX_DEVEYE_FRAMES-1) */
    int x;           /* Draw X (top-left corner, after centering) */
    int y;           /* Draw Y (top-left corner, after centering) */
    int active;      /* 1 = should draw, 0 = animation idle */
} sfx_deveye_info_t;

/* =========================================================================
 * Callback table — injected at creation time
 * ========================================================================= */

typedef struct
{
    /* Move the play window to (x, y) — used by SHAKE and reset */
    void (*on_move_window)(int x, int y, void *ud);

    /* Sound effect requested */
    void (*on_sound)(const char *name, void *ud);
} sfx_system_callbacks_t;

/* =========================================================================
 * Random function type — injectable for deterministic testing
 * ========================================================================= */

typedef int (*sfx_rand_fn)(void);

/* =========================================================================
 * Opaque context
 * ========================================================================= */

typedef struct sfx_system sfx_system_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/*
 * Create an SFX system context.
 *
 * callbacks: callback table (copied).  NULL is safe.
 * user_data: opaque pointer passed to all callbacks.
 * rand_fn:   random number generator (NULL = stdlib rand).
 *
 * Initial state: SFX_MODE_NONE, effects enabled.
 * Returns NULL on allocation failure.
 */
sfx_system_t *sfx_system_create(const sfx_system_callbacks_t *callbacks, void *user_data,
                                sfx_rand_fn rand_fn);

/* Destroy the SFX system.  Safe to call with NULL. */
void sfx_system_destroy(sfx_system_t *ctx);

/* =========================================================================
 * Enable / disable
 * ========================================================================= */

/* Enable or disable special effects (1=on, 0=off) */
void sfx_system_set_enabled(sfx_system_t *ctx, int enabled);

/* Return whether effects are enabled */
int sfx_system_get_enabled(const sfx_system_t *ctx);

/* =========================================================================
 * Mode management
 * ========================================================================= */

/* Activate a new effect mode.  Resets internal effect state. */
void sfx_system_set_mode(sfx_system_t *ctx, sfx_mode_t mode);

/* Return the current effect mode */
sfx_mode_t sfx_system_get_mode(const sfx_system_t *ctx);

/* Set the end frame for timed effects (SHAKE, STATIC) */
void sfx_system_set_end_frame(sfx_system_t *ctx, int end_frame);

/* =========================================================================
 * Per-frame update
 * ========================================================================= */

/*
 * Advance the current effect by one frame.
 *
 * frame: current frame counter.
 *
 * Returns 1 if the effect is still running, 0 if finished.
 * When an effect finishes, mode resets to SFX_MODE_NONE and
 * on_move_window is called to restore the canonical position.
 */
int sfx_system_update(sfx_system_t *ctx, int frame);

/* =========================================================================
 * Effect state queries (for rendering)
 * ========================================================================= */

/* SHAKE: get the current window position.  Only valid during SFX_MODE_SHAKE. */
sfx_shake_pos_t sfx_system_get_shake_pos(const sfx_system_t *ctx);

/* FADE: get the current fade frame info.  Only valid during SFX_MODE_FADE. */
sfx_fade_frame_t sfx_system_get_fade_frame(const sfx_system_t *ctx);

/*
 * SHATTER: fill an array of tiles for the scatter copy sequence.
 *
 * tiles:     output array (caller-allocated).
 * max_tiles: capacity of the tiles array.
 * play_w:    play area width.
 * play_h:    play area height.
 *
 * Returns the number of tiles written.
 * Only valid during SFX_MODE_SHATTER.
 */
int sfx_system_get_shatter_tiles(sfx_system_t *ctx, sfx_shatter_tile_t *tiles, int max_tiles,
                                 int play_w, int play_h);

/*
 * BLIND: fill an array of strip positions for the blind reveal.
 *
 * strips:     output array (caller-allocated).
 * max_strips: capacity of the strips array.
 * play_w:     play area width.
 * play_h:     play area height.
 *
 * Returns the number of strips written.
 * Only valid during SFX_MODE_BLIND.
 */
int sfx_system_get_blind_strips(const sfx_system_t *ctx, sfx_blind_strip_t *strips, int max_strips,
                                int play_w, int play_h);

/* =========================================================================
 * BorderGlow (ambient animation, independent of effect mode)
 * ========================================================================= */

/*
 * Advance the border glow animation by one frame.
 *
 * frame: current frame counter.
 *
 * Returns the current glow state for rendering.  The caller should
 * only update the border color when this returns a changed state.
 */
sfx_glow_state_t sfx_system_update_glow(sfx_system_t *ctx, int frame);

/* Return the current glow state without advancing the animation. */
sfx_glow_state_t sfx_system_get_glow_state(const sfx_system_t *ctx);

/* Reset the border glow to its initial state (red, index 0). */
void sfx_system_reset_glow(sfx_system_t *ctx);

/* =========================================================================
 * FadeAwayArea (stateless utility)
 * ========================================================================= */

/*
 * Compute the number of clear-rect commands for a fade-away area.
 *
 * w: area width.
 *
 * Returns the number of steps (w / SFX_FADEAWAY_STRIDE + 1, approximately).
 */
int sfx_system_fadeaway_steps(int w);

/* =========================================================================
 * Devil eyes blink animation
 * ========================================================================= */

/*
 * Advance the devil eyes blink animation by one step.
 *
 * play_w: play area width (for positioning).
 * play_h: play area height (for positioning).
 *
 * Returns render info with sprite frame index and position.
 * When the 26-step sequence completes, active is set to 0 and
 * the animation resets for the next trigger.
 *
 * Returns 1 if animation is still running, 0 if sequence complete.
 */
int sfx_system_update_deveyes(sfx_system_t *ctx, int play_w, int play_h);

/*
 * Get the current devil eyes render info.
 *
 * Returns frame index and position for the integration layer to draw.
 */
sfx_deveye_info_t sfx_system_get_deveye_info(const sfx_system_t *ctx);

/*
 * Start a new devil eyes blink sequence.
 *
 * Resets the animation to the beginning of the 26-step sequence.
 */
void sfx_system_start_deveyes(sfx_system_t *ctx);

#endif /* SFX_SYSTEM_H */
