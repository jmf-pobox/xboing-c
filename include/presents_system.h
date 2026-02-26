/*
 * presents_system.h — Pure C presents/splash screen sequencer.
 *
 * Owns the 14-state animation sequence that plays on game startup:
 * flag + earth, author credits, "XBOING" letter stamps, sparkle,
 * typewriter welcome text, and curtain-wipe clear.
 *
 * No X11 or SDL2 dependency.  Rendering decisions are communicated
 * via query functions that return position/frame/text data for the
 * integration layer to draw.
 */

#ifndef PRESENTS_SYSTEM_H
#define PRESENTS_SYSTEM_H

#include <stddef.h>

/* =========================================================================
 * Constants (match legacy presents.c / stage.h)
 * ========================================================================= */

#define PRESENTS_MAIN_WIDTH 70
#define PRESENTS_MAIN_HEIGHT 130
#define PRESENTS_PLAY_WIDTH 495
#define PRESENTS_PLAY_HEIGHT 580
#define PRESENTS_TOTAL_WIDTH (PRESENTS_MAIN_WIDTH + PRESENTS_PLAY_WIDTH)
#define PRESENTS_TOTAL_HEIGHT (PRESENTS_MAIN_HEIGHT + PRESENTS_PLAY_HEIGHT)

#define PRESENTS_GAP 10
#define PRESENTS_SPARKLE_FRAMES 11
#define PRESENTS_TITLE_LETTERS 6
#define PRESENTS_WIPE_STEP 10
#define PRESENTS_TYPEWRITER_LINES 3

/* Letter widths for X, B, O, I, N, G (legacy dists[]) */
#define PRESENTS_LETTER_X_W 71
#define PRESENTS_LETTER_B_W 73
#define PRESENTS_LETTER_O_W 83
#define PRESENTS_LETTER_I_W 41
#define PRESENTS_LETTER_N_W 85
#define PRESENTS_LETTER_G_W 88
#define PRESENTS_LETTER_HEIGHT 74

/* Timing delays (in frames) */
#define PRESENTS_FLAG_DELAY 800
#define PRESENTS_TEXT1_DELAY 300
#define PRESENTS_TEXT2_DELAY 500
#define PRESENTS_TEXT3_DELAY 750
#define PRESENTS_TEXT_CLEAR_DELAY 10
#define PRESENTS_LETTER_DELAY 300
#define PRESENTS_AFTER_II_DELAY 200
#define PRESENTS_SPARKLE_STEP 35
#define PRESENTS_AFTER_SPARKLE_DELAY 500
#define PRESENTS_TYPEWRITER_CHAR_DELAY 30
#define PRESENTS_AFTER_LINE_DELAY 700
#define PRESENTS_AFTER_LAST_LINE_DELAY 800
#define PRESENTS_WIPE_DELAY 20
#define PRESENTS_RESET_DELAY 100

/* =========================================================================
 * Types
 * ========================================================================= */

typedef enum
{
    PRESENTS_STATE_NONE = 0,
    PRESENTS_STATE_FLAG,
    PRESENTS_STATE_TEXT1,
    PRESENTS_STATE_TEXT2,
    PRESENTS_STATE_TEXT3,
    PRESENTS_STATE_TEXT_CLEAR,
    PRESENTS_STATE_LETTERS,
    PRESENTS_STATE_SHINE,
    PRESENTS_STATE_SPECIAL_TEXT1,
    PRESENTS_STATE_SPECIAL_TEXT2,
    PRESENTS_STATE_SPECIAL_TEXT3,
    PRESENTS_STATE_CLEAR,
    PRESENTS_STATE_FINISH,
    PRESENTS_STATE_WAIT
} presents_state_t;

/* What the integration layer should render for the FLAG state. */
typedef struct
{
    int flag_x;
    int flag_y;
    int earth_x;
    int earth_y;
    int copyright_y;
} presents_flag_info_t;

/* Info for rendering a single title letter (XBOING). */
typedef struct
{
    int letter_index; /* 0=X, 1=B, 2=O, 3=I, 4=N, 5=G */
    int x;
    int y;
    int width;
    int height;
} presents_letter_info_t;

/* Info for the "II" suffix drawn after all 6 XBOING letters. */
typedef struct
{
    int i1_x;
    int i2_x;
    int y;
    int width;
    int height;
} presents_ii_info_t;

/* Info for the sparkle animation. */
typedef struct
{
    int x;
    int y;
    int frame_index; /* 0..10 */
    int active;      /* 1 = draw sparkle, 0 = idle/done */
    int first_frame; /* 1 = save background this frame */
    int restore;     /* 1 = restore background (sparkle finished) */
} presents_sparkle_info_t;

/* Info for one line of typewriter text. */
typedef struct
{
    const char *text;
    int x_offset; /* horizontal centering offset (pixels) */
    int y;
    int chars_visible; /* how many characters to draw (1..len) */
    int complete;      /* 1 = all characters revealed */
} presents_typewriter_info_t;

/* Info for the curtain-wipe clear. */
typedef struct
{
    int top_y;
    int bottom_y;
    int total_width;
    int complete; /* 1 = wipe reached center */
} presents_wipe_info_t;

/* Sound event produced by the sequencer. */
typedef struct
{
    const char *name; /* sound file name, or NULL if no sound this frame */
    int volume;       /* 0-100 */
} presents_sound_t;

/* Callback table for side effects. */
typedef struct
{
    /* Called when the sequence finishes. */
    void (*on_finished)(void *user_data);

    /* Query: get the user's display name for the welcome message.
     * Returns pointer to static/persistent string.  NULL = use fallback. */
    const char *(*get_nickname)(void *user_data);
    const char *(*get_fullname)(void *user_data);
} presents_system_callbacks_t;

/* Opaque context. */
typedef struct presents_system presents_system_t;

/* =========================================================================
 * API
 * ========================================================================= */

presents_system_t *presents_system_create(const presents_system_callbacks_t *callbacks,
                                          void *user_data);
void presents_system_destroy(presents_system_t *ctx);

/* Begin the sequence.  frame = current global frame counter. */
void presents_system_begin(presents_system_t *ctx, int frame);

/* Advance one frame.  Returns 1 if the sequence is still active, 0 if done. */
int presents_system_update(presents_system_t *ctx, int frame);

/* Skip to finish (user pressed space). */
void presents_system_skip(presents_system_t *ctx, int frame);

/* Query current state. */
presents_state_t presents_system_get_state(const presents_system_t *ctx);
int presents_system_is_finished(const presents_system_t *ctx);

/* Render-info queries — call after update(). */
void presents_system_get_flag_info(const presents_system_t *ctx, presents_flag_info_t *out);
int presents_system_get_letter_info(const presents_system_t *ctx, presents_letter_info_t *out);
int presents_system_get_ii_info(const presents_system_t *ctx, presents_ii_info_t *out);
void presents_system_get_sparkle_info(const presents_system_t *ctx, presents_sparkle_info_t *out);
int presents_system_get_typewriter_info(const presents_system_t *ctx, int line_index,
                                        presents_typewriter_info_t *out);
void presents_system_get_wipe_info(const presents_system_t *ctx, presents_wipe_info_t *out);
presents_sound_t presents_system_get_sound(const presents_system_t *ctx);

/* Query: which typewriter line index (0-2) is currently active, or -1. */
int presents_system_get_active_typewriter_line(const presents_system_t *ctx);

#endif /* PRESENTS_SYSTEM_H */
