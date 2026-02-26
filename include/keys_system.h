/*
 * keys_system.h — Pure C keys and level editor controls screen sequencer.
 *
 * Owns two screen sequences:
 *   - GAME:   game control listing + mouse diagram + sparkle loop
 *   - EDITOR: editor instructions + editor control listing + sparkle loop
 *
 * No X11 or SDL2 dependency.  Rendering via query functions.
 */

#ifndef KEYS_SYSTEM_H
#define KEYS_SYSTEM_H

/* =========================================================================
 * Constants
 * ========================================================================= */

#define KEYS_PLAY_WIDTH 495
#define KEYS_PLAY_HEIGHT 580

#define KEYS_SPARKLE_FRAMES 11
#define KEYS_SPARKLE_STEP 15
#define KEYS_SPARKLE_PAUSE 500

#define KEYS_END_FRAME_OFFSET 4000
#define KEYS_SPARKLE_DELAY 100
#define KEYS_BLINK_GAP 10
#define KEYS_FLASH 30
#define KEYS_GAP 12

/* Number of game key bindings (two columns, 10 each). */
#define KEYS_GAME_BINDINGS_COUNT 20

/* Number of editor info text lines. */
#define KEYS_EDITOR_INFO_COUNT 7

/* Number of editor key bindings (two columns, 5 each). */
#define KEYS_EDITOR_BINDINGS_COUNT 10

/* Column x positions for game key bindings. */
#define KEYS_GAME_LEFT_X 30
#define KEYS_GAME_RIGHT_X 280

/* Column x positions for editor key bindings. */
#define KEYS_EDITOR_LEFT_X 30
#define KEYS_EDITOR_RIGHT_X 270

/* =========================================================================
 * Types
 * ========================================================================= */

typedef enum
{
    KEYS_MODE_GAME = 0,
    KEYS_MODE_EDITOR
} keys_screen_mode_t;

typedef enum
{
    KEYS_STATE_NONE = 0,
    KEYS_STATE_TITLE,
    KEYS_STATE_TEXT,
    KEYS_STATE_SPARKLE,
    KEYS_STATE_WAIT,
    KEYS_STATE_FINISH
} keys_state_t;

/* One key binding entry. */
typedef struct
{
    const char *text; /* e.g., "<s> = Sfx On/Off" */
    int column;       /* 0 = left, 1 = right */
} keys_binding_entry_t;

/* One line of editor info text. */
typedef struct
{
    const char *text;
} keys_info_line_t;

/* Sparkle info. */
typedef struct
{
    int x;
    int y;
    int frame_index;
    int active;
    int save_bg;
    int restore_bg;
} keys_sparkle_info_t;

/* Sound event. */
typedef struct
{
    const char *name;
    int volume;
} keys_sound_t;

/* Callback table. */
typedef struct
{
    void (*on_finished)(keys_screen_mode_t mode, void *user_data);
} keys_system_callbacks_t;

typedef int (*keys_rand_fn)(void *user_data);

/* Opaque context. */
typedef struct keys_system keys_system_t;

/* =========================================================================
 * API
 * ========================================================================= */

keys_system_t *keys_system_create(const keys_system_callbacks_t *callbacks, void *user_data,
                                  keys_rand_fn rand_fn);
void keys_system_destroy(keys_system_t *ctx);

void keys_system_begin(keys_system_t *ctx, keys_screen_mode_t mode, int frame);
int keys_system_update(keys_system_t *ctx, int frame);

keys_state_t keys_system_get_state(const keys_system_t *ctx);
keys_screen_mode_t keys_system_get_mode(const keys_system_t *ctx);
int keys_system_is_finished(const keys_system_t *ctx);

/* Game key bindings (20 entries). */
int keys_system_get_game_bindings(const keys_system_t *ctx, const keys_binding_entry_t **out);

/* Editor info text (7 lines). */
int keys_system_get_editor_info(const keys_system_t *ctx, const keys_info_line_t **out);

/* Editor key bindings (10 entries). */
int keys_system_get_editor_bindings(const keys_system_t *ctx, const keys_binding_entry_t **out);

/* Sparkle info. */
void keys_system_get_sparkle_info(const keys_system_t *ctx, keys_sparkle_info_t *out);

/* Should blink this frame? (game mode only) */
int keys_system_should_blink(const keys_system_t *ctx);

/* Should draw specials this frame? */
int keys_system_should_draw_specials(const keys_system_t *ctx);

/* Sound for current frame. */
keys_sound_t keys_system_get_sound(const keys_system_t *ctx);

#endif /* KEYS_SYSTEM_H */
