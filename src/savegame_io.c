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
 * Internal: JSON writing
 * ========================================================================= */

static int write_savegame_json(FILE *fp, const savegame_data_t *data)
{
    if (fprintf(fp,
                "{\n"
                "  \"version\": %d,\n"
                "  \"score\": %lu,\n"
                "  \"level\": %lu,\n"
                "  \"level_time\": %d,\n"
                "  \"game_time\": %lu,\n"
                "  \"lives_left\": %d,\n"
                "  \"start_level\": %d,\n"
                "  \"paddle_size\": %d,\n"
                "  \"num_bullets\": %d\n"
                "}\n",
                SAVEGAME_IO_VERSION, data->score, data->level, data->level_time, data->game_time,
                data->lives_left, data->start_level, data->paddle_size, data->num_bullets) < 0)
    {
        return -1;
    }
    return 0;
}

/* =========================================================================
 * Internal: JSON reading
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

static long read_long(FILE *fp)
{
    long val = 0;
    int sign = 1;
    int c = skip_ws(fp);

    if (c == '-')
    {
        sign = -1;
        c = fgetc(fp);
    }

    while (c >= '0' && c <= '9')
    {
        val = val * 10 + (long)(c - '0');
        c = fgetc(fp);
    }

    if (c != EOF)
    {
        ungetc(c, fp);
    }
    return val * sign;
}

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

        /* Skip to colon. */
        while ((c = fgetc(fp)) != EOF && c != ':')
        {
        }
        if (c == EOF)
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
        else if (strcmp(key, "paddle_size") == 0)
        {
            data->paddle_size = (int)read_long(fp);
        }
        else if (strcmp(key, "num_bullets") == 0)
        {
            data->num_bullets = (int)read_long(fp);
        }
        else
        {
            /* Unknown key — skip value. */
            c = skip_ws(fp);
            if (c == '"')
            {
                char tmp[256];
                read_json_string(fp, tmp, (int)sizeof(tmp));
            }
            /* else it was a number, already consumed by read_long */
        }
    }

    if (version != SAVEGAME_IO_VERSION)
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

/* =========================================================================
 * Public API
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

    ensure_parent_dir(path);

    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *fp = fopen(tmp_path, "w");
    if (!fp)
    {
        return SAVEGAME_IO_ERR_OPEN;
    }

    if (write_savegame_json(fp, data) < 0)
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
