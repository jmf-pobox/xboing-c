/*
 * intro_system.h — Pure C intro and instructions screen sequencer.
 *
 * Owns two related screen sequences:
 *   - INTRO: background + title + block descriptions + sparkle loop
 *   - INSTRUCT: title + instruction text + sparkle loop
 *
 * Both share the sparkle/star animation.  No X11 or SDL2 dependency.
 * Rendering communicated via query functions.
 */

#ifndef INTRO_SYSTEM_H
#define INTRO_SYSTEM_H

/* =========================================================================
 * Constants (match legacy intro.c / inst.c / stage.h)
 * ========================================================================= */

#define INTRO_PLAY_WIDTH 495
#define INTRO_PLAY_HEIGHT 580
#define INTRO_TITLE_WIDTH 474
#define INTRO_TITLE_HEIGHT 74

#define INTRO_SPARKLE_FRAMES 11
#define INTRO_SPARKLE_STEP 15
#define INTRO_SPARKLE_PAUSE 500

#define INTRO_BLINK_GAP 1000
#define INTRO_BLINK_RATE 25

#define INTRO_END_FRAME_OFFSET 3000
#define INSTRUCT_END_FRAME_OFFSET 7000

/* Block description table dimensions */
#define INTRO_BLOCK_ROWS_LEFT 11
#define INTRO_BLOCK_ROWS_RIGHT 11
#define INTRO_BLOCK_TOTAL (INTRO_BLOCK_ROWS_LEFT + INTRO_BLOCK_ROWS_RIGHT)
#define INTRO_BLOCK_START_Y 120
#define INTRO_BLOCK_STEP_Y 40
#define INTRO_BLOCK_LEFT_X 40
#define INTRO_BLOCK_RIGHT_X 260
#define INTRO_BLOCK_TEXT_OFFSET 60

/* Instruction text */
#define INSTRUCT_TEXT_LINES 20
#define INSTRUCT_GAP 5

/* =========================================================================
 * Types
 * ========================================================================= */

typedef enum
{
    INTRO_MODE_INTRO = 0,
    INTRO_MODE_INSTRUCT
} intro_screen_mode_t;

/* Intro screen states */
typedef enum
{
    INTRO_STATE_NONE = 0,
    INTRO_STATE_TITLE,
    INTRO_STATE_BLOCKS,
    INTRO_STATE_TEXT,
    INTRO_STATE_SPARKLE,
    INTRO_STATE_FINISH,
    INTRO_STATE_WAIT
} intro_state_t;

/* Block type identifiers matching legacy block_type enum values.
 * Integration layer maps these to actual block pixmaps. */
typedef enum
{
    INTRO_BLK_RED = 0,
    INTRO_BLK_ROAMER,
    INTRO_BLK_PAD_EXPAND,
    INTRO_BLK_BONUSX2,
    INTRO_BLK_MAXAMMO,
    INTRO_BLK_DROP,
    INTRO_BLK_BULLET,
    INTRO_BLK_HYPERSPACE,
    INTRO_BLK_REVERSE,
    INTRO_BLK_MGUN,
    INTRO_BLK_MULTIBALL,
    INTRO_BLK_BONUS,
    INTRO_BLK_COUNTER,
    INTRO_BLK_TIMER,
    INTRO_BLK_BLACK,
    INTRO_BLK_BOMB,
    INTRO_BLK_PADDLE,
    INTRO_BLK_BULLET_ITEM,
    INTRO_BLK_DEATH,
    INTRO_BLK_EXTRABALL,
    INTRO_BLK_WALLOFF,
    INTRO_BLK_STICKY
} intro_block_type_t;

/* One entry in the block description table. */
typedef struct
{
    intro_block_type_t type;
    int x;
    int y;
    int x_adjust;
    int y_adjust;
    const char *description;
} intro_block_entry_t;

/* Sparkle animation state for querying. */
typedef struct
{
    int x;
    int y;
    int frame_index;
    int active;
    int save_bg;
    int restore_bg;
} intro_sparkle_info_t;

/* One line of instruction text. */
typedef struct
{
    const char *text;
    int is_spacer;
    int color_index;
} intro_instruct_line_t;

/* Sound event. */
typedef struct
{
    const char *name;
    int volume;
} intro_sound_t;

/* Callback table. */
typedef struct
{
    void (*on_finished)(intro_screen_mode_t mode, void *user_data);
} intro_system_callbacks_t;

typedef int (*intro_rand_fn)(void *user_data);

/* Opaque context. */
typedef struct intro_system intro_system_t;

/* =========================================================================
 * API
 * ========================================================================= */

intro_system_t *intro_system_create(const intro_system_callbacks_t *callbacks, void *user_data,
                                    intro_rand_fn rand_fn);
void intro_system_destroy(intro_system_t *ctx);

/* Begin a screen sequence.  mode selects intro vs instructions. */
void intro_system_begin(intro_system_t *ctx, intro_screen_mode_t mode, int frame);

/* Advance one frame.  Returns 1 if active, 0 if done. */
int intro_system_update(intro_system_t *ctx, int frame);

/* Query state. */
intro_state_t intro_system_get_state(const intro_system_t *ctx);
intro_screen_mode_t intro_system_get_mode(const intro_system_t *ctx);
int intro_system_is_finished(const intro_system_t *ctx);

/* Block description table (22 entries).  Returns count. */
int intro_system_get_block_table(const intro_system_t *ctx, const intro_block_entry_t **out);

/* Instruction text (20 lines).  Returns count. */
int intro_system_get_instruct_text(const intro_system_t *ctx, const intro_instruct_line_t **out);

/* Sparkle info for current frame. */
void intro_system_get_sparkle_info(const intro_system_t *ctx, intro_sparkle_info_t *out);

/* Sound event for current frame. */
intro_sound_t intro_system_get_sound(const intro_system_t *ctx);

/* Devil eyes blink: returns 1 if blink should fire this frame. */
int intro_system_should_blink(const intro_system_t *ctx);

/* Special effects flash: returns 1 if random specials should draw. */
int intro_system_should_draw_specials(const intro_system_t *ctx);

#endif /* INTRO_SYSTEM_H */
