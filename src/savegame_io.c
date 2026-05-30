/*
 * savegame_io.c — JSON-based save/load game state I/O.
 *
 * See savegame_io.h for module overview.
 */

#include "savegame_io.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* =========================================================================
 * Internal: tokenizer helpers
 * ========================================================================= */

static int skip_ws(FILE *fp)
{
    int c;
    while ((c = fgetc(fp)) != EOF)
    {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
        {
            return c;
        }
    }
    return EOF;
}

static int read_json_string(FILE *fp, char *buf, int buf_size)
{
    int len = 0;
    int c;
    while ((c = fgetc(fp)) != EOF)
    {
        if (c == '"')
        {
            if (len < buf_size)
            {
                buf[len] = '\0';
            }
            else if (buf_size > 0)
            {
                buf[buf_size - 1] = '\0';
            }
            return len;
        }
        if (c == '\\')
        {
            c = fgetc(fp);
            if (c == EOF)
            {
                return -1;
            }
        }
        if (len < buf_size - 1)
        {
            buf[len++] = (char)c;
        }
    }
    return -1;
}

/* Forward decl — read_long calls skip_value when it sees a non-numeric
 * value to keep parser state consistent for the next key/value pair. */
static int skip_value(FILE *fp);

static long read_long(FILE *fp)
{
    long val = 0;
    int sign = 1;
    int has_digit = 0;
    int c = skip_ws(fp);

    if (c == '-')
    {
        sign = -1;
        c = fgetc(fp);
    }

    while (c >= '0' && c <= '9')
    {
        has_digit = 1;
        val = val * 10 + (long)(c - '0');
        c = fgetc(fp);
    }

    if (c != EOF)
    {
        ungetc(c, fp);
    }

    if (!has_digit)
    {
        /* Non-numeric value where an int was expected.  Consume the
         * value so the parser stays aligned at the next comma/brace. */
        (void)skip_value(fp);
        return 0;
    }

    return val * sign;
}

/* Advance past the colon following a key.  Returns 0 on success, -1 on EOF. */
static int skip_to_colon(FILE *fp)
{
    int c;
    while ((c = fgetc(fp)) != EOF)
    {
        if (c == ':')
        {
            return 0;
        }
    }
    return -1;
}

/* Skip whatever JSON value comes next (string, number, object, array). */
static int skip_value(FILE *fp)
{
    int c = skip_ws(fp);
    if (c == EOF)
    {
        return -1;
    }
    if (c == '"')
    {
        char tmp[256];
        return read_json_string(fp, tmp, (int)sizeof(tmp)) < 0 ? -1 : 0;
    }
    if (c == '{' || c == '[')
    {
        int open_char = c;
        int close_char = (c == '{') ? '}' : ']';
        int depth = 1;
        while (depth > 0)
        {
            c = fgetc(fp);
            if (c == EOF)
            {
                return -1;
            }
            if (c == '"')
            {
                char tmp[256];
                if (read_json_string(fp, tmp, (int)sizeof(tmp)) < 0)
                {
                    return -1;
                }
                continue;
            }
            if (c == open_char)
            {
                depth++;
            }
            else if (c == close_char)
            {
                depth--;
            }
        }
        return 0;
    }
    /* number / true / false / null — consume until terminator */
    while ((c = fgetc(fp)) != EOF)
    {
        if (c == ',' || c == '}' || c == ']')
        {
            ungetc(c, fp);
            return 0;
        }
    }
    return -1;
}

/* =========================================================================
 * Internal: nested object readers
 * ========================================================================= */

static int read_specials_obj(FILE *fp, savegame_specials_t *out)
{
    char key[32];
    int c = skip_ws(fp);
    if (c != '{')
    {
        return -1;
    }
    for (;;)
    {
        c = skip_ws(fp);
        if (c == '}')
        {
            return 0;
        }
        if (c == ',')
        {
            c = skip_ws(fp);
        }
        if (c == '}')
        {
            return 0;
        }
        if (c != '"')
        {
            return -1;
        }
        if (read_json_string(fp, key, (int)sizeof(key)) < 0)
        {
            return -1;
        }
        if (skip_to_colon(fp) < 0)
        {
            return -1;
        }
        if (strcmp(key, "sticky") == 0)
        {
            out->sticky = (int)read_long(fp);
        }
        else if (strcmp(key, "saving") == 0)
        {
            out->saving = (int)read_long(fp);
        }
        else if (strcmp(key, "fast_gun") == 0)
        {
            out->fast_gun = (int)read_long(fp);
        }
        else if (strcmp(key, "no_walls") == 0)
        {
            out->no_walls = (int)read_long(fp);
        }
        else if (strcmp(key, "killer") == 0)
        {
            out->killer = (int)read_long(fp);
        }
        else if (strcmp(key, "x2") == 0)
        {
            out->x2 = (int)read_long(fp);
        }
        else if (strcmp(key, "x4") == 0)
        {
            out->x4 = (int)read_long(fp);
        }
        else
        {
            if (skip_value(fp) < 0)
            {
                return -1;
            }
        }
    }
}

static int read_eyedude_obj(FILE *fp, savegame_eyedude_t *out)
{
    char key[32];
    int c = skip_ws(fp);
    if (c != '{')
    {
        return -1;
    }
    for (;;)
    {
        c = skip_ws(fp);
        if (c == '}')
        {
            return 0;
        }
        if (c == ',')
        {
            c = skip_ws(fp);
        }
        if (c == '}')
        {
            return 0;
        }
        if (c != '"')
        {
            return -1;
        }
        if (read_json_string(fp, key, (int)sizeof(key)) < 0)
        {
            return -1;
        }
        if (skip_to_colon(fp) < 0)
        {
            return -1;
        }
        if (strcmp(key, "state") == 0)
        {
            out->state = (int)read_long(fp);
        }
        else if (strcmp(key, "dir") == 0)
        {
            out->dir = (int)read_long(fp);
        }
        else if (strcmp(key, "x") == 0)
        {
            out->x = (int)read_long(fp);
        }
        else if (strcmp(key, "y") == 0)
        {
            out->y = (int)read_long(fp);
        }
        else if (strcmp(key, "slide") == 0)
        {
            out->slide = (int)read_long(fp);
        }
        else if (strcmp(key, "inc") == 0)
        {
            out->inc = (int)read_long(fp);
        }
        else if (strcmp(key, "turn") == 0)
        {
            out->turn = (int)read_long(fp);
        }
        else
        {
            if (skip_value(fp) < 0)
            {
                return -1;
            }
        }
    }
}

static int read_ball_obj(FILE *fp, savegame_ball_t *out)
{
    char key[32];
    int c = skip_ws(fp);
    if (c != '{')
    {
        return -1;
    }
    for (;;)
    {
        c = skip_ws(fp);
        if (c == '}')
        {
            return 0;
        }
        if (c == ',')
        {
            c = skip_ws(fp);
        }
        if (c == '}')
        {
            return 0;
        }
        if (c != '"')
        {
            return -1;
        }
        if (read_json_string(fp, key, (int)sizeof(key)) < 0)
        {
            return -1;
        }
        if (skip_to_colon(fp) < 0)
        {
            return -1;
        }
        if (strcmp(key, "active") == 0)
        {
            out->active = (int)read_long(fp);
        }
        else if (strcmp(key, "state") == 0)
        {
            out->state = (int)read_long(fp);
        }
        else if (strcmp(key, "x") == 0)
        {
            out->x = (int)read_long(fp);
        }
        else if (strcmp(key, "y") == 0)
        {
            out->y = (int)read_long(fp);
        }
        else if (strcmp(key, "dx") == 0)
        {
            out->dx = (int)read_long(fp);
        }
        else if (strcmp(key, "dy") == 0)
        {
            out->dy = (int)read_long(fp);
        }
        else if (strcmp(key, "wait_mode") == 0)
        {
            out->wait_mode = (int)read_long(fp);
        }
        else
        {
            if (skip_value(fp) < 0)
            {
                return -1;
            }
        }
    }
}

static int read_balls_array(FILE *fp, savegame_ball_t *balls, int max)
{
    int c = skip_ws(fp);
    if (c != '[')
    {
        return -1;
    }
    int idx = 0;
    for (;;)
    {
        c = skip_ws(fp);
        if (c == ']')
        {
            return 0;
        }
        if (c == ',')
        {
            c = skip_ws(fp);
        }
        if (c == ']')
        {
            return 0;
        }
        if (c != '{')
        {
            return -1;
        }
        ungetc(c, fp);
        if (idx < max)
        {
            if (read_ball_obj(fp, &balls[idx]) < 0)
            {
                return -1;
            }
            idx++;
        }
        else
        {
            if (skip_value(fp) < 0)
            {
                return -1;
            }
        }
    }
}

/* =========================================================================
 * Internal: save-info writer
 * ========================================================================= */

static int write_specials(FILE *fp, const savegame_specials_t *s)
{
    return fprintf(fp,
                   "  \"specials\": {\n"
                   "    \"sticky\": %d, \"saving\": %d, \"fast_gun\": %d, "
                   "\"no_walls\": %d,\n"
                   "    \"killer\": %d, \"x2\": %d, \"x4\": %d\n"
                   "  },\n",
                   s->sticky, s->saving, s->fast_gun, s->no_walls, s->killer, s->x2, s->x4) < 0
               ? -1
               : 0;
}

static int write_eyedude(FILE *fp, const savegame_eyedude_t *e)
{
    return fprintf(fp,
                   "  \"eyedude\": {\n"
                   "    \"state\": %d, \"dir\": %d, \"x\": %d, \"y\": %d,\n"
                   "    \"slide\": %d, \"inc\": %d, \"turn\": %d\n"
                   "  },\n",
                   e->state, e->dir, e->x, e->y, e->slide, e->inc, e->turn) < 0
               ? -1
               : 0;
}

static int write_balls(FILE *fp, const savegame_ball_t *balls)
{
    if (fprintf(fp, "  \"balls\": [\n") < 0)
    {
        return -1;
    }
    for (int i = 0; i < MAX_BALLS; i++)
    {
        const savegame_ball_t *b = &balls[i];
        if (fprintf(fp,
                    "    {\"active\": %d, \"state\": %d, \"x\": %d, \"y\": %d, "
                    "\"dx\": %d, \"dy\": %d, \"wait_mode\": %d}%s\n",
                    b->active, b->state, b->x, b->y, b->dx, b->dy, b->wait_mode,
                    (i == MAX_BALLS - 1) ? "" : ",") < 0)
        {
            return -1;
        }
    }
    if (fprintf(fp, "  ]\n") < 0)
    {
        return -1;
    }
    return 0;
}

static int write_savegame_json(FILE *fp, const savegame_data_t *data)
{
    if (fprintf(fp,
                "{\n"
                "  \"version\": %d,\n"
                "  \"score\": %lu,\n"
                "  \"level\": %lu,\n"
                "  \"level_time\": %d,\n"
                "  \"time_remaining\": %d,\n"
                "  \"game_time\": %lu,\n"
                "  \"lives_left\": %d,\n"
                "  \"start_level\": %d,\n"
                "  \"user_tilts\": %d,\n"
                "  \"bonus_count\": %d,\n"
                "  \"paddle_pos\": %d,\n"
                "  \"paddle_size_type\": %d,\n"
                "  \"paddle_size\": %d,\n"
                "  \"paddle_reverse\": %d,\n"
                "  \"paddle_sticky\": %d,\n"
                "  \"num_bullets\": %d,\n"
                "  \"gun_unlimited\": %d,\n",
                SAVEGAME_IO_VERSION, data->score, data->level, data->level_time,
                data->time_remaining, data->game_time, data->lives_left, data->start_level,
                data->user_tilts, data->bonus_count, data->paddle_pos, data->paddle_size_type,
                data->paddle_size, data->paddle_reverse, data->paddle_sticky, data->num_bullets,
                data->gun_unlimited) < 0)
    {
        return -1;
    }
    if (write_specials(fp, &data->specials) < 0)
    {
        return -1;
    }
    if (write_eyedude(fp, &data->eyedude) < 0)
    {
        return -1;
    }
    if (write_balls(fp, data->balls) < 0)
    {
        return -1;
    }
    if (fprintf(fp, "}\n") < 0)
    {
        return -1;
    }
    return 0;
}

/* =========================================================================
 * Internal: save-info reader
 * ========================================================================= */

static int read_savegame_json(FILE *fp, savegame_data_t *data)
{
    char key[32];
    int c;
    int version = -1;

    c = skip_ws(fp);
    if (c != '{')
    {
        return -1;
    }

    for (;;)
    {
        c = skip_ws(fp);
        if (c == '}')
        {
            break;
        }
        if (c == ',')
        {
            c = skip_ws(fp);
        }
        if (c == '}')
        {
            break;
        }
        if (c != '"')
        {
            return -1;
        }

        if (read_json_string(fp, key, (int)sizeof(key)) < 0)
        {
            return -1;
        }

        if (skip_to_colon(fp) < 0)
        {
            return -1;
        }

        if (strcmp(key, "version") == 0)
        {
            version = (int)read_long(fp);
        }
        else if (strcmp(key, "score") == 0)
        {
            data->score = (unsigned long)read_long(fp);
        }
        else if (strcmp(key, "level") == 0)
        {
            data->level = (unsigned long)read_long(fp);
        }
        else if (strcmp(key, "level_time") == 0)
        {
            data->level_time = (int)read_long(fp);
        }
        else if (strcmp(key, "time_remaining") == 0)
        {
            data->time_remaining = (int)read_long(fp);
        }
        else if (strcmp(key, "game_time") == 0)
        {
            data->game_time = (unsigned long)read_long(fp);
        }
        else if (strcmp(key, "lives_left") == 0)
        {
            data->lives_left = (int)read_long(fp);
        }
        else if (strcmp(key, "start_level") == 0)
        {
            data->start_level = (int)read_long(fp);
        }
        else if (strcmp(key, "user_tilts") == 0)
        {
            data->user_tilts = (int)read_long(fp);
        }
        else if (strcmp(key, "bonus_count") == 0)
        {
            data->bonus_count = (int)read_long(fp);
        }
        else if (strcmp(key, "paddle_pos") == 0)
        {
            data->paddle_pos = (int)read_long(fp);
        }
        else if (strcmp(key, "paddle_size_type") == 0)
        {
            data->paddle_size_type = (int)read_long(fp);
        }
        else if (strcmp(key, "paddle_size") == 0)
        {
            data->paddle_size = (int)read_long(fp);
        }
        else if (strcmp(key, "paddle_reverse") == 0)
        {
            data->paddle_reverse = (int)read_long(fp);
        }
        else if (strcmp(key, "paddle_sticky") == 0)
        {
            data->paddle_sticky = (int)read_long(fp);
        }
        else if (strcmp(key, "num_bullets") == 0)
        {
            data->num_bullets = (int)read_long(fp);
        }
        else if (strcmp(key, "gun_unlimited") == 0)
        {
            data->gun_unlimited = (int)read_long(fp);
        }
        else if (strcmp(key, "specials") == 0)
        {
            if (read_specials_obj(fp, &data->specials) < 0)
            {
                return -1;
            }
        }
        else if (strcmp(key, "eyedude") == 0)
        {
            if (read_eyedude_obj(fp, &data->eyedude) < 0)
            {
                return -1;
            }
        }
        else if (strcmp(key, "balls") == 0)
        {
            if (read_balls_array(fp, data->balls, MAX_BALLS) < 0)
            {
                return -1;
            }
        }
        else
        {
            if (skip_value(fp) < 0)
            {
                return -1;
            }
        }
    }

    if (version != SAVEGAME_IO_VERSION)
    {
        return -2;
    }

    return 0;
}

/* =========================================================================
 * Internal: save-level reader/writer
 * ========================================================================= */

static int write_cell(FILE *fp, int r, int c, const savegame_cell_t *cell, int is_last)
{
    return fprintf(fp,
                   "    {\"r\": %d, \"c\": %d, \"type\": %d, \"counter_slide\": %d, "
                   "\"random\": %d, \"hit_points\": %d, \"next_frame_off\": %d}%s\n",
                   r, c, cell->block_type, cell->counter_slide, cell->random, cell->hit_points,
                   cell->next_frame_offset, is_last ? "" : ",") < 0
               ? -1
               : 0;
}

static int write_level_json(FILE *fp, const savegame_level_t *level)
{
    /* Count occupied cells so we know which is last (for trailing comma). */
    int total_occupied = 0;
    for (int r = 0; r < MAX_ROW; r++)
    {
        for (int c = 0; c < MAX_COL; c++)
        {
            if (level->cells[r][c].occupied)
            {
                total_occupied++;
            }
        }
    }

    /* JSON-escape the title (only handle backslash and quote).
     * Output buffer must hold at most 2x input plus terminator since
     * every input char could be escaped. */
    char title_escaped[LEVEL_TITLE_MAX * 2 + 1];
    {
        size_t out_idx = 0;
        for (size_t i = 0; i < LEVEL_TITLE_MAX && level->title[i] != '\0'; i++)
        {
            char ch = level->title[i];
            if (ch == '\\' || ch == '"')
            {
                title_escaped[out_idx++] = '\\';
            }
            title_escaped[out_idx++] = ch;
        }
        title_escaped[out_idx] = '\0';
    }

    if (fprintf(fp,
                "{\n"
                "  \"version\": %d,\n"
                "  \"title\": \"%s\",\n"
                "  \"time_bonus\": %d,\n"
                "  \"cells\": [\n",
                SAVEGAME_LEVEL_VERSION, title_escaped, level->time_bonus) < 0)
    {
        return -1;
    }

    int written = 0;
    for (int r = 0; r < MAX_ROW; r++)
    {
        for (int c = 0; c < MAX_COL; c++)
        {
            if (!level->cells[r][c].occupied)
            {
                continue;
            }
            written++;
            if (write_cell(fp, r, c, &level->cells[r][c], written == total_occupied) < 0)
            {
                return -1;
            }
        }
    }

    if (fprintf(fp, "  ]\n}\n") < 0)
    {
        return -1;
    }
    return 0;
}

static int read_cell_obj(FILE *fp, int *out_r, int *out_c, savegame_cell_t *out)
{
    char key[32];
    int c = skip_ws(fp);
    if (c != '{')
    {
        return -1;
    }
    *out_r = -1;
    *out_c = -1;
    for (;;)
    {
        c = skip_ws(fp);
        if (c == '}')
        {
            break;
        }
        if (c == ',')
        {
            c = skip_ws(fp);
        }
        if (c == '}')
        {
            break;
        }
        if (c != '"')
        {
            return -1;
        }
        if (read_json_string(fp, key, (int)sizeof(key)) < 0)
        {
            return -1;
        }
        if (skip_to_colon(fp) < 0)
        {
            return -1;
        }
        if (strcmp(key, "r") == 0)
        {
            *out_r = (int)read_long(fp);
        }
        else if (strcmp(key, "c") == 0)
        {
            *out_c = (int)read_long(fp);
        }
        else if (strcmp(key, "type") == 0)
        {
            out->block_type = (int)read_long(fp);
        }
        else if (strcmp(key, "counter_slide") == 0)
        {
            out->counter_slide = (int)read_long(fp);
        }
        else if (strcmp(key, "random") == 0)
        {
            out->random = (int)read_long(fp);
        }
        else if (strcmp(key, "hit_points") == 0)
        {
            out->hit_points = (int)read_long(fp);
        }
        else if (strcmp(key, "next_frame_off") == 0)
        {
            out->next_frame_offset = (int)read_long(fp);
        }
        else
        {
            if (skip_value(fp) < 0)
            {
                return -1;
            }
        }
    }
    if (*out_r < 0 || *out_r >= MAX_ROW || *out_c < 0 || *out_c >= MAX_COL)
    {
        return -1;
    }
    out->occupied = 1;
    return 0;
}

static int read_cells_array(FILE *fp, savegame_level_t *level)
{
    int c = skip_ws(fp);
    if (c != '[')
    {
        return -1;
    }
    for (;;)
    {
        c = skip_ws(fp);
        if (c == ']')
        {
            return 0;
        }
        if (c == ',')
        {
            c = skip_ws(fp);
        }
        if (c == ']')
        {
            return 0;
        }
        if (c != '{')
        {
            return -1;
        }
        ungetc(c, fp);

        savegame_cell_t cell = {0};
        int r = -1, col = -1;
        if (read_cell_obj(fp, &r, &col, &cell) < 0)
        {
            return -1;
        }
        level->cells[r][col] = cell;
    }
}

static int read_level_json(FILE *fp, savegame_level_t *level)
{
    char key[32];
    int c;
    int version = -1;

    c = skip_ws(fp);
    if (c != '{')
    {
        return -1;
    }

    for (;;)
    {
        c = skip_ws(fp);
        if (c == '}')
        {
            break;
        }
        if (c == ',')
        {
            c = skip_ws(fp);
        }
        if (c == '}')
        {
            break;
        }
        if (c != '"')
        {
            return -1;
        }

        if (read_json_string(fp, key, (int)sizeof(key)) < 0)
        {
            return -1;
        }

        if (skip_to_colon(fp) < 0)
        {
            return -1;
        }

        if (strcmp(key, "version") == 0)
        {
            version = (int)read_long(fp);
        }
        else if (strcmp(key, "title") == 0)
        {
            c = skip_ws(fp);
            if (c != '"')
            {
                return -1;
            }
            if (read_json_string(fp, level->title, LEVEL_TITLE_MAX) < 0)
            {
                return -1;
            }
        }
        else if (strcmp(key, "time_bonus") == 0)
        {
            level->time_bonus = (int)read_long(fp);
        }
        else if (strcmp(key, "cells") == 0)
        {
            if (read_cells_array(fp, level) < 0)
            {
                return -1;
            }
        }
        else
        {
            if (skip_value(fp) < 0)
            {
                return -1;
            }
        }
    }

    if (version != SAVEGAME_LEVEL_VERSION)
    {
        return -2;
    }
    return 0;
}

/* =========================================================================
 * Internal: directory creation
 * ========================================================================= */

static void ensure_parent_dir(const char *path)
{
    char buf[1024];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *sep = strrchr(buf, '/');
    if (!sep || sep == buf)
    {
        return;
    }
    *sep = '\0';

    struct stat st;
    if (stat(buf, &st) == 0)
    {
        return;
    }

    ensure_parent_dir(buf);
    (void)mkdir(buf, 0755);
}

/* Generic atomic write helper. */
typedef int (*write_fn_t)(FILE *, const void *);

static savegame_io_result_t write_file_atomic(const char *path, const void *data, write_fn_t writer)
{
    char tmp_path[1024];
    /* Leave room for ".tmp" suffix (4 chars) plus NUL terminator. */
    if (strlen(path) + 5 > sizeof(tmp_path))
    {
        return SAVEGAME_IO_ERR_OPEN;
    }

    ensure_parent_dir(path);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *fp = fopen(tmp_path, "w");
    if (!fp)
    {
        return SAVEGAME_IO_ERR_OPEN;
    }

    if (writer(fp, data) < 0)
    {
        fclose(fp);
        (void)remove(tmp_path);
        return SAVEGAME_IO_ERR_WRITE;
    }

    if (fclose(fp) != 0)
    {
        (void)remove(tmp_path);
        return SAVEGAME_IO_ERR_WRITE;
    }

    if (rename(tmp_path, path) != 0)
    {
        (void)remove(tmp_path);
        return SAVEGAME_IO_ERR_RENAME;
    }

    return SAVEGAME_IO_OK;
}

/* Adapters for write_fn_t signature. */
static int writer_savegame(FILE *fp, const void *data)
{
    return write_savegame_json(fp, (const savegame_data_t *)data);
}

static int writer_level(FILE *fp, const void *data)
{
    return write_level_json(fp, (const savegame_level_t *)data);
}

/* =========================================================================
 * Public API — save-info
 * ========================================================================= */

savegame_io_result_t savegame_io_read(const char *path, savegame_data_t *data)
{
    if (!path || !data)
    {
        return SAVEGAME_IO_ERR_NULL;
    }

    savegame_io_init(data);

    FILE *fp = fopen(path, "r");
    if (!fp)
    {
        return SAVEGAME_IO_ERR_OPEN;
    }

    int result = read_savegame_json(fp, data);
    fclose(fp);

    if (result == -2)
    {
        savegame_io_init(data);
        return SAVEGAME_IO_ERR_VERSION;
    }
    if (result < 0)
    {
        savegame_io_init(data);
        return SAVEGAME_IO_ERR_READ;
    }

    return SAVEGAME_IO_OK;
}

savegame_io_result_t savegame_io_write(const char *path, const savegame_data_t *data)
{
    if (!path || !data)
    {
        return SAVEGAME_IO_ERR_NULL;
    }
    return write_file_atomic(path, data, writer_savegame);
}

int savegame_io_exists(const char *path)
{
    if (!path)
    {
        return 0;
    }
    struct stat st;
    return stat(path, &st) == 0 ? 1 : 0;
}

savegame_io_result_t savegame_io_delete(const char *path)
{
    if (!path)
    {
        return SAVEGAME_IO_ERR_NULL;
    }
    if (remove(path) != 0 && errno != ENOENT)
    {
        return SAVEGAME_IO_ERR_OPEN;
    }
    return SAVEGAME_IO_OK;
}

void savegame_io_init(savegame_data_t *data)
{
    if (!data)
    {
        return;
    }
    memset(data, 0, sizeof(*data));
    data->level = 1;
    data->lives_left = 3;
    data->start_level = 1;
    data->paddle_size = 50;
}

/* =========================================================================
 * Public API — save-level
 * ========================================================================= */

savegame_io_result_t savegame_level_read(const char *path, savegame_level_t *level)
{
    if (!path || !level)
    {
        return SAVEGAME_IO_ERR_NULL;
    }

    savegame_level_init(level);

    FILE *fp = fopen(path, "r");
    if (!fp)
    {
        return SAVEGAME_IO_ERR_OPEN;
    }

    int result = read_level_json(fp, level);
    fclose(fp);

    if (result == -2)
    {
        savegame_level_init(level);
        return SAVEGAME_IO_ERR_VERSION;
    }
    if (result < 0)
    {
        savegame_level_init(level);
        return SAVEGAME_IO_ERR_READ;
    }

    return SAVEGAME_IO_OK;
}

savegame_io_result_t savegame_level_write(const char *path, const savegame_level_t *level)
{
    if (!path || !level)
    {
        return SAVEGAME_IO_ERR_NULL;
    }
    return write_file_atomic(path, level, writer_level);
}

void savegame_level_init(savegame_level_t *level)
{
    if (!level)
    {
        return;
    }
    memset(level, 0, sizeof(*level));
}
