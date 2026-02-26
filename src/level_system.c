/*
 * level_system.c — Pure C level file loading with callback-based block creation.
 *
 * Owns level file parsing, character-to-block-type mapping, level number
 * wrapping, and background cycling.  Delegates block creation to the
 * integration layer via an injected callback.  Zero dependency on SDL2 or X11.
 *
 * Opaque context pattern: no globals, fully testable with CMocka stubs.
 * See ADR-020 in docs/DESIGN.md for design rationale.
 */

#include "level_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Internal structure
 * ========================================================================= */

struct level_system
{
    char title[LEVEL_TITLE_MAX];
    int time_bonus;
    int background; /* Current background (1 initially, 2..5 after advance) */
    level_system_callbacks_t callbacks;
    void *user_data;
};

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

level_system_t *level_system_create(const level_system_callbacks_t *callbacks, void *user_data,
                                    level_system_status_t *status)
{
    level_system_t *ctx = calloc(1, sizeof(level_system_t));
    if (ctx == NULL)
    {
        if (status)
        {
            *status = LEVEL_SYS_ERR_ALLOC_FAILED;
        }
        return NULL;
    }

    ctx->user_data = user_data;

    if (callbacks)
    {
        ctx->callbacks = *callbacks;
    }

    ctx->title[0] = '\0';
    ctx->time_bonus = 0;
    ctx->background = 1; /* First advance will go to 2 */

    if (status)
    {
        *status = LEVEL_SYS_OK;
    }
    return ctx;
}

void level_system_destroy(level_system_t *ctx)
{
    free(ctx);
}

/* =========================================================================
 * Character-to-block mapping
 * ========================================================================= */

int level_system_char_to_block(char ch, int *out_counter_slide)
{
    int slide = 0;
    int block_type = NONE_BLK;

    switch (ch)
    {
        case 'H':
            block_type = HYPERSPACE_BLK;
            break;
        case 'B':
            block_type = BULLET_BLK;
            break;
        case 'c':
            block_type = MAXAMMO_BLK;
            break;
        case 'r':
            block_type = RED_BLK;
            break;
        case 'g':
            block_type = GREEN_BLK;
            break;
        case 'b':
            block_type = BLUE_BLK;
            break;
        case 't':
            block_type = TAN_BLK;
            break;
        case 'p':
            block_type = PURPLE_BLK;
            break;
        case 'y':
            block_type = YELLOW_BLK;
            break;
        case 'w':
            block_type = BLACK_BLK;
            break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
            block_type = COUNTER_BLK;
            slide = ch - '0';
            break;
        case '+':
            block_type = ROAMER_BLK;
            break;
        case 'X':
            block_type = BOMB_BLK;
            break;
        case 'D':
            block_type = DEATH_BLK;
            slide = LEVEL_SHOTS_TO_KILL;
            break;
        case 'L':
            block_type = EXTRABALL_BLK;
            break;
        case 'M':
            block_type = MGUN_BLK;
            slide = LEVEL_SHOTS_TO_KILL;
            break;
        case 'W':
            block_type = WALLOFF_BLK;
            slide = LEVEL_SHOTS_TO_KILL;
            break;
        case '?':
            block_type = RANDOM_BLK;
            break;
        case 'd':
            block_type = DROP_BLK;
            break;
        case 'T':
            block_type = TIMER_BLK;
            break;
        case 'm':
            block_type = MULTIBALL_BLK;
            slide = LEVEL_SHOTS_TO_KILL;
            break;
        case 's':
            block_type = STICKY_BLK;
            slide = LEVEL_SHOTS_TO_KILL;
            break;
        case 'R':
            block_type = REVERSE_BLK;
            slide = LEVEL_SHOTS_TO_KILL;
            break;
        case '<':
            block_type = PAD_SHRINK_BLK;
            slide = LEVEL_SHOTS_TO_KILL;
            break;
        case '>':
            block_type = PAD_EXPAND_BLK;
            slide = LEVEL_SHOTS_TO_KILL;
            break;
        default:
            /* '.' and any unknown character → empty cell */
            block_type = NONE_BLK;
            break;
    }

    if (out_counter_slide)
    {
        *out_counter_slide = slide;
    }
    return block_type;
}

/* =========================================================================
 * Level loading
 * ========================================================================= */

level_system_status_t level_system_load_file(level_system_t *ctx, const char *path)
{
    if (ctx == NULL || path == NULL)
    {
        return LEVEL_SYS_ERR_NULL_ARG;
    }

    FILE *fp = fopen(path, "r");
    if (fp == NULL)
    {
        return LEVEL_SYS_ERR_FILE_NOT_FOUND;
    }

    /* Line 1: Title */
    char buf[1024];
    if (fgets(buf, (int)sizeof(buf), fp) == NULL)
    {
        fclose(fp);
        return LEVEL_SYS_ERR_PARSE_FAILED;
    }

    /* Strip trailing newline (guard against missing \n) */
    char *nl = strchr(buf, '\n');
    if (nl)
    {
        *nl = '\0';
    }

    /* Copy title, truncating if necessary */
    strncpy(ctx->title, buf, LEVEL_TITLE_MAX - 1);
    ctx->title[LEVEL_TITLE_MAX - 1] = '\0';

    /* Line 2: Time bonus */
    if (fgets(buf, (int)sizeof(buf), fp) == NULL)
    {
        fclose(fp);
        return LEVEL_SYS_ERR_PARSE_FAILED;
    }

    int time_val = 0;
    if (sscanf(buf, "%d", &time_val) != 1)
    {
        fclose(fp);
        return LEVEL_SYS_ERR_PARSE_FAILED;
    }
    ctx->time_bonus = time_val;

    /* Lines 3-17: 15 rows of 9 characters */
    for (int row = 0; row < LEVEL_GRID_ROWS; row++)
    {
        for (int col = 0; col < LEVEL_GRID_COLS; col++)
        {
            int ch = fgetc(fp);
            if (ch == EOF)
            {
                fclose(fp);
                return LEVEL_SYS_ERR_PARSE_FAILED;
            }

            char c = (char)ch;
            int counter_slide = 0;
            int block_type = level_system_char_to_block(c, &counter_slide);

            if (block_type != NONE_BLK && ctx->callbacks.on_add_block)
            {
                ctx->callbacks.on_add_block(row, col, block_type, counter_slide, ctx->user_data);
            }
        }

        /* Consume the newline after each row */
        (void)fgetc(fp);
    }

    fclose(fp);
    return LEVEL_SYS_OK;
}

/* =========================================================================
 * Background cycling
 * ========================================================================= */

int level_system_advance_background(level_system_t *ctx)
{
    if (ctx == NULL)
    {
        return LEVEL_BG_FIRST;
    }

    ctx->background++;
    if (ctx->background > LEVEL_BG_LAST)
    {
        ctx->background = LEVEL_BG_FIRST;
    }

    return ctx->background;
}

int level_system_get_background(const level_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 1;
    }
    return ctx->background;
}

/* =========================================================================
 * Queries
 * ========================================================================= */

const char *level_system_get_title(const level_system_t *ctx)
{
    if (ctx == NULL)
    {
        return "";
    }
    return ctx->title;
}

int level_system_get_time_bonus(const level_system_t *ctx)
{
    if (ctx == NULL)
    {
        return 0;
    }
    return ctx->time_bonus;
}

/* =========================================================================
 * Utilities
 * ========================================================================= */

int level_system_wrap_number(int level_number)
{
    int wrapped = level_number % LEVEL_MAX_NUM;
    if (wrapped == 0)
    {
        wrapped = LEVEL_MAX_NUM;
    }
    return wrapped;
}

const char *level_system_status_string(level_system_status_t status)
{
    switch (status)
    {
        case LEVEL_SYS_OK:
            return "LEVEL_SYS_OK";
        case LEVEL_SYS_ERR_NULL_ARG:
            return "LEVEL_SYS_ERR_NULL_ARG";
        case LEVEL_SYS_ERR_ALLOC_FAILED:
            return "LEVEL_SYS_ERR_ALLOC_FAILED";
        case LEVEL_SYS_ERR_FILE_NOT_FOUND:
            return "LEVEL_SYS_ERR_FILE_NOT_FOUND";
        case LEVEL_SYS_ERR_PARSE_FAILED:
            return "LEVEL_SYS_ERR_PARSE_FAILED";
        default:
            return "LEVEL_SYS_UNKNOWN";
    }
}
