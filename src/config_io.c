/*
 * config_io.c — TOML-based user preferences I/O.
 *
 * See config_io.h for module overview.
 *
 * TOML subset supported:
 *   - Flat key = value pairs (no tables, no arrays)
 *   - Integer values: key = 42
 *   - Boolean values: key = true / key = false
 *   - String values: key = "text" (with \" and \\ escapes)
 *   - Comments: # to end of line
 *   - Blank lines are ignored
 */

#include "config_io.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
 * Internal: TOML line parser
 * ========================================================================= */

/* Parse a single TOML line into key and value components.
 * Returns 0 on success (key/value extracted), -1 if blank/comment/error.
 * key_buf and val_buf are filled with null-terminated strings.
 * For string values, the surrounding quotes are stripped.
 * val_type is set to: 's' (string), 'i' (integer), 'b' (boolean). */
static int parse_toml_line(const char *line, char *key_buf, int key_size, char *val_buf,
                           int val_size, char *val_type)
{
    const char *p = line;

    /* Skip leading whitespace. */
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }

    /* Blank line or comment. */
    if (*p == '\0' || *p == '\n' || *p == '\r' || *p == '#')
    {
        return -1;
    }

    /* Read key (bare key: alphanumeric + _ + -). */
    int klen = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '=')
    {
        if (klen < key_size - 1)
        {
            key_buf[klen++] = *p;
        }
        p++;
    }
    key_buf[klen] = '\0';

    if (klen == 0)
    {
        return -1;
    }

    /* Skip whitespace before '='. */
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }

    if (*p != '=')
    {
        return -1;
    }
    p++;

    /* Skip whitespace after '='. */
    while (*p == ' ' || *p == '\t')
    {
        p++;
    }

    /* Determine value type and extract. */
    if (*p == '"')
    {
        /* String value. */
        *val_type = 's';
        p++; /* Skip opening quote. */
        int vlen = 0;
        while (*p && *p != '"')
        {
            if (*p == '\\' && *(p + 1))
            {
                p++;
                char esc = *p;
                if (esc == 'n')
                {
                    esc = '\n';
                }
                else if (esc == 't')
                {
                    esc = '\t';
                }
                /* else: literal (handles \" and \\) */
                if (vlen < val_size - 1)
                {
                    val_buf[vlen++] = esc;
                }
            }
            else
            {
                if (vlen < val_size - 1)
                {
                    val_buf[vlen++] = *p;
                }
            }
            p++;
        }
        val_buf[vlen] = '\0';
    }
    else if (strncmp(p, "true", 4) == 0)
    {
        *val_type = 'b';
        strncpy(val_buf, "true", (size_t)val_size);
        val_buf[val_size - 1] = '\0';
    }
    else if (strncmp(p, "false", 5) == 0)
    {
        *val_type = 'b';
        strncpy(val_buf, "false", (size_t)val_size);
        val_buf[val_size - 1] = '\0';
    }
    else if (*p == '-' || (*p >= '0' && *p <= '9'))
    {
        /* Integer value. */
        *val_type = 'i';
        int vlen = 0;
        while (*p == '-' || (*p >= '0' && *p <= '9'))
        {
            if (vlen < val_size - 1)
            {
                val_buf[vlen++] = *p;
            }
            p++;
        }
        val_buf[vlen] = '\0';
    }
    else
    {
        return -1;
    }

    return 0;
}

/* =========================================================================
 * Internal: TOML reading
 * ========================================================================= */

static int read_config_toml(FILE *fp, config_data_t *data)
{
    char line[512];
    char key[64];
    char val[256];
    char val_type;
    int found_any = 0;

    while (fgets(line, (int)sizeof(line), fp) != NULL)
    {
        if (parse_toml_line(line, key, (int)sizeof(key), val, (int)sizeof(val), &val_type) != 0)
        {
            continue;
        }

        found_any = 1;

        if (strcmp(key, "speed") == 0 && val_type == 'i')
        {
            int v = atoi(val);
            if (v >= 1 && v <= 9)
            {
                data->speed = v;
            }
        }
        else if (strcmp(key, "start_level") == 0 && val_type == 'i')
        {
            int v = atoi(val);
            if (v >= 1 && v <= 80)
            {
                data->start_level = v;
            }
        }
        else if (strcmp(key, "control") == 0 && val_type == 's')
        {
            if (strcmp(val, "keys") == 0)
            {
                data->use_keys = true;
            }
            else if (strcmp(val, "mouse") == 0)
            {
                data->use_keys = false;
            }
            /* Unknown control value: keep default. */
        }
        else if (strcmp(key, "sfx") == 0 && val_type == 'b')
        {
            data->sfx = (strcmp(val, "true") == 0);
        }
        else if (strcmp(key, "sound") == 0 && val_type == 'b')
        {
            data->sound = (strcmp(val, "true") == 0);
        }
        else if (strcmp(key, "max_volume") == 0 && val_type == 'i')
        {
            int v = atoi(val);
            if (v >= 0 && v <= 100)
            {
                data->max_volume = v;
            }
        }
        else if (strcmp(key, "nickname") == 0 && val_type == 's')
        {
            strncpy(data->nickname, val, CONFIG_IO_MAX_NICKNAME_LEN);
            data->nickname[CONFIG_IO_MAX_NICKNAME_LEN] = '\0';
        }
        /* Unknown keys are silently ignored (forward compatibility). */
    }

    /* An empty file or comment-only file is valid — defaults are fine. */
    (void)found_any;
    return ferror(fp) ? -1 : 0;
}

/* =========================================================================
 * Internal: TOML writing
 * ========================================================================= */

static int write_config_toml(FILE *fp, const config_data_t *data)
{
    if (fprintf(fp, "# XBoing configuration (v%d)\n", CONFIG_IO_VERSION) < 0)
    {
        return -1;
    }
    if (fprintf(fp, "# Edit this file to change persistent preferences.\n") < 0)
    {
        return -1;
    }
    if (fprintf(fp, "# CLI flags override these values at startup.\n\n") < 0)
    {
        return -1;
    }

    if (fprintf(fp, "speed = %d\n", data->speed) < 0)
    {
        return -1;
    }
    if (fprintf(fp, "start_level = %d\n", data->start_level) < 0)
    {
        return -1;
    }
    if (fprintf(fp, "control = \"%s\"\n", data->use_keys ? "keys" : "mouse") < 0)
    {
        return -1;
    }
    if (fprintf(fp, "sfx = %s\n", data->sfx ? "true" : "false") < 0)
    {
        return -1;
    }
    if (fprintf(fp, "sound = %s\n", data->sound ? "true" : "false") < 0)
    {
        return -1;
    }
    if (fprintf(fp, "max_volume = %d\n", data->max_volume) < 0)
    {
        return -1;
    }

    /* Escape nickname for TOML string. */
    if (fprintf(fp, "nickname = \"") < 0)
    {
        return -1;
    }
    for (const char *p = data->nickname; *p; p++)
    {
        if (*p == '"')
        {
            if (fputs("\\\"", fp) == EOF)
            {
                return -1;
            }
        }
        else if (*p == '\\')
        {
            if (fputs("\\\\", fp) == EOF)
            {
                return -1;
            }
        }
        else
        {
            if (fputc(*p, fp) == EOF)
            {
                return -1;
            }
        }
    }
    if (fprintf(fp, "\"\n") < 0)
    {
        return -1;
    }

    return 0;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

config_io_result_t config_io_read(const char *path, config_data_t *data)
{
    if (!path || !data)
    {
        return CONFIG_IO_ERR_NULL;
    }

    config_io_init(data);

    FILE *fp = fopen(path, "r");
    if (!fp)
    {
        return CONFIG_IO_ERR_OPEN;
    }

    int result = read_config_toml(fp, data);
    fclose(fp);

    if (result < 0)
    {
        config_io_init(data);
        return CONFIG_IO_ERR_READ;
    }

    return CONFIG_IO_OK;
}

config_io_result_t config_io_write(const char *path, const config_data_t *data)
{
    if (!path || !data)
    {
        return CONFIG_IO_ERR_NULL;
    }

    ensure_parent_dir(path);

    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *fp = fopen(tmp_path, "w");
    if (!fp)
    {
        return CONFIG_IO_ERR_OPEN;
    }

    if (write_config_toml(fp, data) < 0)
    {
        fclose(fp);
        (void)remove(tmp_path);
        return CONFIG_IO_ERR_WRITE;
    }

    if (fclose(fp) != 0)
    {
        (void)remove(tmp_path);
        return CONFIG_IO_ERR_WRITE;
    }

    if (rename(tmp_path, path) != 0)
    {
        (void)remove(tmp_path);
        return CONFIG_IO_ERR_RENAME;
    }

    return CONFIG_IO_OK;
}

int config_io_exists(const char *path)
{
    if (!path)
    {
        return 0;
    }
    struct stat st;
    return stat(path, &st) == 0 ? 1 : 0;
}

void config_io_init(config_data_t *data)
{
    if (!data)
    {
        return;
    }
    memset(data, 0, sizeof(*data));
    data->speed = CONFIG_IO_DEFAULT_SPEED;
    data->start_level = CONFIG_IO_DEFAULT_START_LEVEL;
    data->use_keys = false;
    data->sfx = true;
    data->sound = true;
    data->max_volume = CONFIG_IO_DEFAULT_MAX_VOLUME;
}
