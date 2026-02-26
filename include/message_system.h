/*
 * message_system.h — Pure C message display system.
 *
 * Manages the single-line message bar shown in the play area.
 * Messages are set with an optional auto-clear delay (CLEAR_DELAY
 * frames).  When the delay expires, the module reverts to a
 * "default message" (typically the level name formatted as
 * "- Level Name -").
 *
 * Zero dependency on X11 or SDL2.  Integration layer reads the
 * current text via get_text() and renders it however it likes.
 *
 * Legacy source: mess.c (160 lines).
 */

#ifndef MESSAGE_SYSTEM_H
#define MESSAGE_SYSTEM_H

/* =========================================================================
 * Constants
 * ========================================================================= */

/* Frames before an auto-clear message reverts to the default. */
#define MESSAGE_CLEAR_DELAY 2000

/* Maximum message length (matches legacy currentMessage[1024]). */
#define MESSAGE_MAX_LEN 1024

/* =========================================================================
 * Types
 * ========================================================================= */

/* Opaque context. */
typedef struct message_system message_system_t;

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

/* Create a new message system.  Returns NULL on allocation failure. */
message_system_t *message_system_create(void);

/* Destroy the message system and free all resources. */
void message_system_destroy(message_system_t *ctx);

/* =========================================================================
 * Setting messages
 * ========================================================================= */

/*
 * Set the current message.  If auto_clear is non-zero, the message
 * will revert to the default message after MESSAGE_CLEAR_DELAY frames.
 * If auto_clear is zero, the message stays until replaced.
 *
 * frame is the current game frame counter.
 */
void message_system_set(message_system_t *ctx, const char *text, int auto_clear, int frame);

/*
 * Set the default message (shown after auto-clear).  Typically the
 * level name formatted as "- Level Name -".  Pass NULL or "" to
 * clear the default (auto-clear will blank the message area).
 */
void message_system_set_default(message_system_t *ctx, const char *text);

/* =========================================================================
 * Per-frame update
 * ========================================================================= */

/*
 * Call once per frame.  Returns 1 if the displayed text changed
 * this frame (caller should re-render), 0 otherwise.
 */
int message_system_update(message_system_t *ctx, int frame);

/* =========================================================================
 * Queries
 * ========================================================================= */

/* Get the current message text.  Never returns NULL (may be ""). */
const char *message_system_get_text(const message_system_t *ctx);

/* Get the default message text.  Never returns NULL (may be ""). */
const char *message_system_get_default(const message_system_t *ctx);

/* Returns 1 if the text changed since the last update. */
int message_system_text_changed(const message_system_t *ctx);

#endif /* MESSAGE_SYSTEM_H */
