/*
 * demo_system.h — Pure C demo and preview screen sequencer.
 *
 * Owns two screen sequences:
 *   - DEMO:    title + gameplay illustration + sparkle loop
 *   - PREVIEW: random level display + timed wait
 *
 * No X11 or SDL2 dependency.  Rendering via query functions.
 */

#ifndef DEMO_SYSTEM_H
#define DEMO_SYSTEM_H

/* =========================================================================
 * Constants
 * ========================================================================= */

#define DEMO_PLAY_WIDTH 495
#define DEMO_PLAY_HEIGHT 580

#define DEMO_SPARKLE_FRAMES 11
#define DEMO_SPARKLE_STEP 15
#define DEMO_SPARKLE_PAUSE 500

#define DEMO_END_FRAME_OFFSET 5000
#define PREVIEW_WAIT_FRAMES 5000

/* Ball animation trail in demo (10 positions). */
#define DEMO_BALL_TRAIL_COUNT 10

/* Number of descriptive text lines in demo. */
#define DEMO_TEXT_LINES 5

/* =========================================================================
 * Types
 * ========================================================================= */

typedef enum
{
    DEMO_MODE_DEMO = 0,
    DEMO_MODE_PREVIEW
} demo_screen_mode_t;

typedef enum
{
    DEMO_STATE_NONE = 0,
    DEMO_STATE_TITLE,
    DEMO_STATE_BLOCKS,
    DEMO_STATE_TEXT,
    DEMO_STATE_SPARKLE,
    DEMO_STATE_WAIT,
    DEMO_STATE_FINISH
} demo_state_t;

/* One ball in the demo animation trail. */
typedef struct
{
    int x;
    int y;
    int frame_index; /* 0-3 ball animation frame */
} demo_ball_pos_t;

/* One line of descriptive text. */
typedef struct
{
    const char *text;
    int x;
    int y;
} demo_text_line_t;

/* Sparkle info. */
typedef struct
{
    int x;
    int y;
    int frame_index;
    int active;
    int save_bg;
    int restore_bg;
} demo_sparkle_info_t;

/* Sound event. */
typedef struct
{
    const char *name;
    int volume;
} demo_sound_t;

/* Callback table. */
typedef struct
{
    void (*on_finished)(demo_screen_mode_t mode, void *user_data);

    /* Preview mode: called to load a random level.
     * level_num is the randomly selected level number. */
    void (*on_load_level)(int level_num, void *user_data);
} demo_system_callbacks_t;

typedef int (*demo_rand_fn)(void *user_data);

/* Opaque context. */
typedef struct demo_system demo_system_t;

/* =========================================================================
 * API
 * ========================================================================= */

demo_system_t *demo_system_create(const demo_system_callbacks_t *callbacks, void *user_data,
                                  demo_rand_fn rand_fn);
void demo_system_destroy(demo_system_t *ctx);

void demo_system_begin(demo_system_t *ctx, demo_screen_mode_t mode, int frame);
int demo_system_update(demo_system_t *ctx, int frame);

demo_state_t demo_system_get_state(const demo_system_t *ctx);
demo_screen_mode_t demo_system_get_mode(const demo_system_t *ctx);
int demo_system_is_finished(const demo_system_t *ctx);

/* Demo ball animation trail (10 entries). */
int demo_system_get_ball_trail(const demo_system_t *ctx, const demo_ball_pos_t **out);

/* Demo descriptive text lines. */
int demo_system_get_demo_text(const demo_system_t *ctx, const demo_text_line_t **out);

/* Preview: which level was selected. */
int demo_system_get_preview_level(const demo_system_t *ctx);

/* Sparkle info. */
void demo_system_get_sparkle_info(const demo_system_t *ctx, demo_sparkle_info_t *out);

/* Sound for current frame. */
demo_sound_t demo_system_get_sound(const demo_system_t *ctx);

/* Should draw specials this frame? */
int demo_system_should_draw_specials(const demo_system_t *ctx);

#endif /* DEMO_SYSTEM_H */
