/*
 * dialogue_system.h — Pure C modal input dialogue system.
 *
 * Provides a text input dialogue with 4 validation modes:
 *   - TEXT:    printable chars (space through 'z')
 *   - NUMERIC: digits only ('0'-'9')
 *   - ALL:     all printable ASCII (space through '~')
 *   - YES_NO:  single char, 'y'/'Y'/'n'/'N' only
 *
 * No X11 or SDL2 dependency.  Rendering via query functions.
 */

#ifndef DIALOGUE_SYSTEM_H
#define DIALOGUE_SYSTEM_H

/* =========================================================================
 * Constants
 * ========================================================================= */

#define DIALOGUE_PLAY_WIDTH 495
#define DIALOGUE_WIDTH 380
#define DIALOGUE_HEIGHT 120

/* Maximum input buffer length. */
#define DIALOGUE_MAX_INPUT 1024

/* Default max visible characters (integration layer may override). */
#define DIALOGUE_DEFAULT_MAX_CHARS 45

/* =========================================================================
 * Types
 * ========================================================================= */

typedef enum
{
    DIALOGUE_ICON_DISK = 1,
    DIALOGUE_ICON_TEXT = 2
} dialogue_icon_t;

typedef enum
{
    DIALOGUE_VALIDATION_TEXT = 1,
    DIALOGUE_VALIDATION_NUMERIC = 2,
    DIALOGUE_VALIDATION_ALL = 3,
    DIALOGUE_VALIDATION_YES_NO = 4
} dialogue_validation_t;

typedef enum
{
    DIALOGUE_STATE_NONE = 0,
    DIALOGUE_STATE_MAP,
    DIALOGUE_STATE_TEXT,
    DIALOGUE_STATE_UNMAP,
    DIALOGUE_STATE_FINISHED
} dialogue_state_t;

/* Key input types. */
typedef enum
{
    DIALOGUE_KEY_CHAR = 0,
    DIALOGUE_KEY_BACKSPACE,
    DIALOGUE_KEY_RETURN,
    DIALOGUE_KEY_ESCAPE
} dialogue_key_type_t;

/* Sound event. */
typedef struct
{
    const char *name;
    int volume;
} dialogue_sound_t;

/* Opaque context. */
typedef struct dialogue_system dialogue_system_t;

/* =========================================================================
 * API
 * ========================================================================= */

dialogue_system_t *dialogue_system_create(void);
void dialogue_system_destroy(dialogue_system_t *ctx);

/* Open a dialogue with a message, icon type, and validation mode. */
void dialogue_system_open(dialogue_system_t *ctx, const char *message, dialogue_icon_t icon,
                          dialogue_validation_t validation);

/* Process one frame of the dialogue state machine. */
int dialogue_system_update(dialogue_system_t *ctx);

/* Feed a key event. */
void dialogue_system_key_input(dialogue_system_t *ctx, dialogue_key_type_t key_type, char ch);

/* Set maximum visible characters (default: DIALOGUE_DEFAULT_MAX_CHARS).
 * Call before open() or integration layer can adjust dynamically. */
void dialogue_system_set_max_chars(dialogue_system_t *ctx, int max_chars);

/* Queries. */
dialogue_state_t dialogue_system_get_state(const dialogue_system_t *ctx);
const char *dialogue_system_get_message(const dialogue_system_t *ctx);
const char *dialogue_system_get_input(const dialogue_system_t *ctx);
int dialogue_system_get_input_length(const dialogue_system_t *ctx);
dialogue_icon_t dialogue_system_get_icon(const dialogue_system_t *ctx);
dialogue_validation_t dialogue_system_get_validation(const dialogue_system_t *ctx);
int dialogue_system_is_finished(const dialogue_system_t *ctx);

/* Was the dialogue cancelled (Escape) vs submitted (Return)? */
int dialogue_system_was_cancelled(const dialogue_system_t *ctx);

/* Sound for current frame (set after key_input or update). */
dialogue_sound_t dialogue_system_get_sound(const dialogue_system_t *ctx);

#endif /* DIALOGUE_SYSTEM_H */
