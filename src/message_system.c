/*
 * message_system.c — Pure C message display system.
 *
 * See message_system.h for module overview.
 */

#include "message_system.h"

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal state
 * ========================================================================= */

struct message_system
{
    char current[MESSAGE_MAX_LEN];
    char default_msg[MESSAGE_MAX_LEN];
    int clear_frame;
    int changed;
};

/* =========================================================================
 * Public API
 * ========================================================================= */

message_system_t *message_system_create(void)
{
    message_system_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
    {
        return NULL;
    }
    ctx->clear_frame = -1;
    return ctx;
}

void message_system_destroy(message_system_t *ctx)
{
    free(ctx);
}

void message_system_set(message_system_t *ctx, const char *text, int auto_clear, int frame)
{
    if (!ctx)
    {
        return;
    }

    if (text)
    {
        strncpy(ctx->current, text, MESSAGE_MAX_LEN - 1);
        ctx->current[MESSAGE_MAX_LEN - 1] = '\0';
    }
    else
    {
        ctx->current[0] = '\0';
    }

    if (auto_clear)
    {
        ctx->clear_frame = frame + MESSAGE_CLEAR_DELAY;
    }
    else
    {
        /* Never auto-clear: set to impossible frame.
         * Legacy used frame - 1 which means "already past". */
        ctx->clear_frame = -1;
    }

    ctx->changed = 1;
}

void message_system_set_default(message_system_t *ctx, const char *text)
{
    if (!ctx)
    {
        return;
    }

    if (text)
    {
        strncpy(ctx->default_msg, text, MESSAGE_MAX_LEN - 1);
        ctx->default_msg[MESSAGE_MAX_LEN - 1] = '\0';
    }
    else
    {
        ctx->default_msg[0] = '\0';
    }
}

int message_system_update(message_system_t *ctx, int frame)
{
    if (!ctx)
    {
        return 0;
    }

    ctx->changed = 0;

    if (ctx->clear_frame >= 0 && frame >= ctx->clear_frame)
    {
        /* Revert to default message. */
        strncpy(ctx->current, ctx->default_msg, MESSAGE_MAX_LEN - 1);
        ctx->current[MESSAGE_MAX_LEN - 1] = '\0';
        ctx->clear_frame = -1;
        ctx->changed = 1;
    }

    return ctx->changed;
}

const char *message_system_get_text(const message_system_t *ctx)
{
    if (!ctx)
    {
        return "";
    }
    return ctx->current;
}

const char *message_system_get_default(const message_system_t *ctx)
{
    if (!ctx)
    {
        return "";
    }
    return ctx->default_msg;
}

int message_system_text_changed(const message_system_t *ctx)
{
    if (!ctx)
    {
        return 0;
    }
    return ctx->changed;
}
