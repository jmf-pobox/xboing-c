/*
 * highscore_system.h — Pure C high score display screen sequencer.
 *
 * Owns the 5-state display sequence:
 *   TITLE -> SHOW -> SPARKLE (title stars + row walk) -> FINISH
 *
 * Score data is provided by the integration layer via set_table().
 * File I/O, sorting, and locking are NOT part of this module.
 *
 * No X11 or SDL2 dependency.  Rendering via query functions.
 */

#ifndef HIGHSCORE_SYSTEM_H
#define HIGHSCORE_SYSTEM_H

/* =========================================================================
 * Constants
 * ========================================================================= */

#define HIGHSCORE_PLAY_WIDTH 495
#define HIGHSCORE_PLAY_HEIGHT 580

#define HIGHSCORE_NUM_ENTRIES 10
#define HIGHSCORE_NAME_LEN 80

#define HIGHSCORE_END_FRAME_OFFSET 4000
#define HIGHSCORE_FLASH 30

/* Title sparkle: 11 frames, alternating between fast and slow. */
#define HIGHSCORE_TITLE_SPARKLE_FRAMES 11
#define HIGHSCORE_TITLE_SPARKLE_FAST 30
#define HIGHSCORE_TITLE_SPARKLE_SLOW 800

/* Row sparkle: 11 frames. */
#define HIGHSCORE_ROW_SPARKLE_FRAMES 11
#define HIGHSCORE_ROW_SPARKLE_STEP 30
#define HIGHSCORE_ROW_SPARKLE_PAUSE 100

/* =========================================================================
 * Types
 * ========================================================================= */

typedef enum
{
    HIGHSCORE_TYPE_GLOBAL = 0,
    HIGHSCORE_TYPE_PERSONAL
} highscore_type_t;

typedef enum
{
    HIGHSCORE_STATE_NONE = 0,
    HIGHSCORE_STATE_TITLE,
    HIGHSCORE_STATE_SHOW,
    HIGHSCORE_STATE_SPARKLE,
    HIGHSCORE_STATE_WAIT,
    HIGHSCORE_STATE_FINISH
} highscore_state_t;

/* One high score entry (host byte order, pre-formatted by integration). */
typedef struct
{
    unsigned long score;
    unsigned long level;
    unsigned long game_time; /* total seconds */
    unsigned long timestamp; /* unix time */
    char name[HIGHSCORE_NAME_LEN];
} highscore_entry_t;

/* Complete table data provided by integration layer. */
typedef struct
{
    char master_name[HIGHSCORE_NAME_LEN];
    char master_text[HIGHSCORE_NAME_LEN];
    highscore_entry_t entries[HIGHSCORE_NUM_ENTRIES];
} highscore_table_t;

/* Title sparkle info (two stars flanking the title). */
typedef struct
{
    int frame_index;  /* 0-10 */
    int mirror_index; /* 10-frame_index (opposite star) */
    int active;       /* 1 if sparkling this frame */
    int clear;        /* 1 if cycle complete, clear stars */
} highscore_title_sparkle_t;

/* Row sparkle info (walks down score rows). */
typedef struct
{
    int row;         /* current row being sparkled (0-9) */
    int frame_index; /* 0-10 animation frame */
    int active;      /* 1 if row sparkle is active */
    int save_bg;     /* 1 if bg should be saved */
    int restore_bg;  /* 1 if bg should be restored */
} highscore_row_sparkle_t;

/* Sound event. */
typedef struct
{
    const char *name;
    int volume;
} highscore_sound_t;

/* Callback table. */
typedef struct
{
    void (*on_finished)(highscore_type_t type, void *user_data);
} highscore_system_callbacks_t;

/* Opaque context. */
typedef struct highscore_system highscore_system_t;

/* =========================================================================
 * API
 * ========================================================================= */

highscore_system_t *highscore_system_create(const highscore_system_callbacks_t *callbacks,
                                            void *user_data);
void highscore_system_destroy(highscore_system_t *ctx);

/* Set the score table data before calling begin(). */
void highscore_system_set_table(highscore_system_t *ctx, const highscore_table_t *table);

/* Set the current player's score for highlighting. */
void highscore_system_set_current_score(highscore_system_t *ctx, unsigned long score);

void highscore_system_begin(highscore_system_t *ctx, highscore_type_t type, int frame);
int highscore_system_update(highscore_system_t *ctx, int frame);

highscore_state_t highscore_system_get_state(const highscore_system_t *ctx);
highscore_type_t highscore_system_get_type(const highscore_system_t *ctx);
int highscore_system_is_finished(const highscore_system_t *ctx);

/* Get the score table data. */
const highscore_table_t *highscore_system_get_table(const highscore_system_t *ctx);

/* Get current player's score for highlight comparison. */
unsigned long highscore_system_get_current_score(const highscore_system_t *ctx);

/* Title sparkle info. */
void highscore_system_get_title_sparkle(const highscore_system_t *ctx,
                                        highscore_title_sparkle_t *out);

/* Row sparkle info. */
void highscore_system_get_row_sparkle(const highscore_system_t *ctx, highscore_row_sparkle_t *out);

/* Should draw specials this frame? */
int highscore_system_should_draw_specials(const highscore_system_t *ctx);

/* Sound for current frame. */
highscore_sound_t highscore_system_get_sound(const highscore_system_t *ctx);

#endif /* HIGHSCORE_SYSTEM_H */
