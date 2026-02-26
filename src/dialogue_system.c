/*
 * dialogue_system.c — Pure C modal input dialogue system.
 *
 * See dialogue_system.h for module overview.
 */

#include "dialogue_system.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal state
 * ========================================================================= */

struct dialogue_system
{
    dialogue_state_t state;

    char message[80];
    char input[DIALOGUE_MAX_INPUT];
    int input_len;
    int max_chars;

    dialogue_icon_t icon;
    dialogue_validation_t validation;

    int cancelled;

    /* Sound. */
    dialogue_sound_t sound;
};

/* =========================================================================
 * Validation
 * ========================================================================= */

static int is_valid_char(dialogue_validation_t validation, char ch)
{
    switch (validation)
    {
        case DIALOGUE_VALIDATION_TEXT:
            /* XK_space (0x20) through XK_z (0x7a). */
            return (ch >= ' ' && ch <= 'z') ? 1 : 0;

        case DIALOGUE_VALIDATION_NUMERIC:
            return (ch >= '0' && ch <= '9') ? 1 : 0;

        case DIALOGUE_VALIDATION_ALL:
            /* XK_space (0x20) through XK_asciitilde (0x7e). */
            return (ch >= ' ' && ch <= '~') ? 1 : 0;

        case DIALOGUE_VALIDATION_YES_NO:
            return (ch == 'y' || ch == 'Y' || ch == 'n' || ch == 'N') ? 1 : 0;
    }
    return 0;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

dialogue_system_t *dialogue_system_create(void)
{
    dialogue_system_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
    {
        return NULL;
    }
    ctx->state = DIALOGUE_STATE_NONE;
    ctx->max_chars = DIALOGUE_DEFAULT_MAX_CHARS;
    return ctx;
}

void dialogue_system_destroy(dialogue_system_t *ctx)
{
    free(ctx);
}

void dialogue_system_open(dialogue_system_t *ctx, const char *message, dialogue_icon_t icon,
                          dialogue_validation_t validation)
{
    if (!ctx)
    {
        return;
    }
    if (message)
    {
        strncpy(ctx->message, message, sizeof(ctx->message) - 1);
        ctx->message[sizeof(ctx->message) - 1] = '\0';
    }
    else
    {
        ctx->message[0] = '\0';
    }
    ctx->icon = icon;
    ctx->validation = validation;
    ctx->input[0] = '\0';
    ctx->input_len = 0;
    ctx->cancelled = 0;
    ctx->state = DIALOGUE_STATE_MAP;
    ctx->sound.name = NULL;
    ctx->sound.volume = 0;
}

int dialogue_system_update(dialogue_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }

    ctx->sound.name = NULL;
    ctx->sound.volume = 0;

    switch (ctx->state)
    {
        case DIALOGUE_STATE_MAP:
            ctx->state = DIALOGUE_STATE_TEXT;
            break;

        case DIALOGUE_STATE_UNMAP:
            ctx->state = DIALOGUE_STATE_FINISHED;
            break;

        case DIALOGUE_STATE_TEXT:
        case DIALOGUE_STATE_FINISHED:
        case DIALOGUE_STATE_NONE:
            break;
    }

    return (ctx->state == DIALOGUE_STATE_FINISHED) ? 0 : 1;
}

void dialogue_system_key_input(dialogue_system_t *ctx, dialogue_key_type_t key_type, char ch)
{
    if (!ctx || ctx->state != DIALOGUE_STATE_TEXT)
    {
        return;
    }

    ctx->sound.name = NULL;
    ctx->sound.volume = 0;

    switch (key_type)
    {
        case DIALOGUE_KEY_ESCAPE:
            ctx->cancelled = 1;
            ctx->state = DIALOGUE_STATE_UNMAP;
            break;

        case DIALOGUE_KEY_RETURN:
            ctx->cancelled = 0;
            ctx->state = DIALOGUE_STATE_UNMAP;
            break;

        case DIALOGUE_KEY_BACKSPACE:
            if (ctx->input_len > 0)
            {
                ctx->input_len--;
                ctx->input[ctx->input_len] = '\0';
                ctx->sound.name = "key";
                ctx->sound.volume = 70;
            }
            break;

        case DIALOGUE_KEY_CHAR:
            if (!is_valid_char(ctx->validation, ch))
            {
                return;
            }

            if (ctx->input_len >= ctx->max_chars)
            {
                /* Buffer full — play overflow sound. */
                ctx->sound.name = "tone";
                ctx->sound.volume = 40;
                return;
            }

            /* YES_NO allows only one character. */
            if (ctx->validation == DIALOGUE_VALIDATION_YES_NO && ctx->input_len > 0)
            {
                return;
            }

            ctx->input[ctx->input_len] = ch;
            ctx->input_len++;
            ctx->input[ctx->input_len] = '\0';
            ctx->sound.name = "click";
            ctx->sound.volume = 70;
            break;
    }
}

void dialogue_system_set_max_chars(dialogue_system_t *ctx, int max_chars)
{
    if (!ctx || max_chars <= 0)
    {
        return;
    }
    if (max_chars >= DIALOGUE_MAX_INPUT)
    {
        max_chars = DIALOGUE_MAX_INPUT - 1;
    }
    ctx->max_chars = max_chars;
}

dialogue_state_t dialogue_system_get_state(const dialogue_system_t *ctx)
{
    if (!ctx)
    {
        return DIALOGUE_STATE_NONE;
    }
    return ctx->state;
}

const char *dialogue_system_get_message(const dialogue_system_t *ctx)
{
    if (!ctx)
    {
        return "";
    }
    return ctx->message;
}

const char *dialogue_system_get_input(const dialogue_system_t *ctx)
{
    if (!ctx)
    {
        return "";
    }
    return ctx->input;
}

int dialogue_system_get_input_length(const dialogue_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }
    return ctx->input_len;
}

dialogue_icon_t dialogue_system_get_icon(const dialogue_system_t *ctx)
{
    if (!ctx)
    {
        return DIALOGUE_ICON_TEXT;
    }
    return ctx->icon;
}

dialogue_validation_t dialogue_system_get_validation(const dialogue_system_t *ctx)
{
    if (!ctx)
    {
        return DIALOGUE_VALIDATION_TEXT;
    }
    return ctx->validation;
}

int dialogue_system_is_finished(const dialogue_system_t *ctx)
{
    if (!ctx)
    {
        return 1;
    }
    return (ctx->state == DIALOGUE_STATE_FINISHED) ? 1 : 0;
}

int dialogue_system_was_cancelled(const dialogue_system_t *ctx)
{
    if (!ctx)
    {
        return 1;
    }
    return ctx->cancelled;
}

dialogue_sound_t dialogue_system_get_sound(const dialogue_system_t *ctx)
{
    dialogue_sound_t none = {NULL, 0};
    if (!ctx)
    {
        return none;
    }
    return ctx->sound;
}
