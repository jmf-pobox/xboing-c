/*
 * highscore_io.c — JSON-based high score file I/O.
 *
 * See highscore_io.h for module overview.
 */

#include "highscore_io.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* =========================================================================
 * Internal: JSON writing helpers
 * ========================================================================= */

/*
 * Write a JSON-escaped string to the file.  Escapes: \, ", and
 * control characters (< 0x20).
 */
static int write_json_string(FILE *fp, const char *s)
{
    if (fputc('"', fp) == EOF)
    {
        return -1;
    }
    for (; *s; s++)
    {
        unsigned char c = (unsigned char)*s;
        if (c == '\\' || c == '"')
        {
            if (fputc('\\', fp) == EOF || fputc((int)c, fp) == EOF)
            {
                return -1;
            }
        }
        else if (c < 0x20)
        {
            if (fprintf(fp, "\\u%04x", c) < 0)
            {
                return -1;
            }
        }
        else
        {
            if (fputc((int)c, fp) == EOF)
            {
                return -1;
            }
        }
    }
    if (fputc('"', fp) == EOF)
    {
        return -1;
    }
    return 0;
}

static int write_table_json(FILE *fp, const highscore_table_t *table)
{
    if (fprintf(fp, "{\n") < 0)
    {
        return -1;
    }
    if (fprintf(fp, "  \"version\": %d,\n", HIGHSCORE_IO_VERSION) < 0)
    {
        return -1;
    }

    if (fprintf(fp, "  \"master_name\": ") < 0)
    {
        return -1;
    }
    if (write_json_string(fp, table->master_name) < 0)
    {
        return -1;
    }
    if (fprintf(fp, ",\n") < 0)
    {
        return -1;
    }

    if (fprintf(fp, "  \"master_text\": ") < 0)
    {
        return -1;
    }
    if (write_json_string(fp, table->master_text) < 0)
    {
        return -1;
    }
    if (fprintf(fp, ",\n") < 0)
    {
        return -1;
    }

    if (fprintf(fp, "  \"entries\": [\n") < 0)
    {
        return -1;
    }

    for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        const highscore_entry_t *e = &table->entries[i];
        if (fprintf(fp,
                    "    {\"score\": %lu, \"level\": %lu, "
                    "\"game_time\": %lu, \"timestamp\": %lu, \"name\": ",
                    e->score, e->level, e->game_time, e->timestamp) < 0)
        {
            return -1;
        }
        if (write_json_string(fp, e->name) < 0)
        {
            return -1;
        }
        if (i < HIGHSCORE_NUM_ENTRIES - 1)
        {
            if (fprintf(fp, "},\n") < 0)
            {
                return -1;
            }
        }
        else
        {
            if (fprintf(fp, "}\n") < 0)
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

/* =========================================================================
 * Internal: JSON reading helpers
 *
 * Minimal parser for the specific JSON format we write.  Not a
 * general-purpose JSON parser — only handles our known schema.
 * ========================================================================= */

/* Skip whitespace, return next char or EOF. */
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

/* Read a JSON string (after opening '"'). Returns length or -1. */
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
            if (c == 'u')
            {
                /* \uXXXX — read 4 hex digits, store as char. */
                char hex[5] = {0};
                for (int h = 0; h < 4; h++)
                {
                    hex[h] = (char)fgetc(fp);
                }
                unsigned int val = 0;
                if (sscanf(hex, "%x", &val) == 1 && len < buf_size - 1)
                {
                    buf[len++] = (char)val;
                }
                continue;
            }
            /* \" or \\ — store the escaped char. */
        }
        if (len < buf_size - 1)
        {
            buf[len++] = (char)c;
        }
    }
    return -1; /* unterminated string */
}

/* Read unsigned long after skipping to digits. Returns 0 on error. */
static unsigned long read_ulong(FILE *fp)
{
    unsigned long val = 0;
    int c = skip_ws(fp);
    if (c == EOF)
    {
        return 0;
    }

    /* Handle digits. */
    while (c >= '0' && c <= '9')
    {
        val = val * 10 + (unsigned long)(c - '0');
        c = fgetc(fp);
    }

    /* Put back the non-digit char. */
    if (c != EOF)
    {
        ungetc(c, fp);
    }
    return val;
}

/* Skip to the next ':' (value separator). Returns 0 on success. */
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

/* Skip to the next occurrence of ch. Returns 0 on success. */
static int skip_to(FILE *fp, int ch)
{
    int c;
    while ((c = fgetc(fp)) != EOF)
    {
        if (c == ch)
        {
            return 0;
        }
    }
    return -1;
}

static int read_table_json(FILE *fp, highscore_table_t *table)
{
    char key[64];
    int c;
    int version = -1;

    /* Expect opening '{'. */
    c = skip_ws(fp);
    if (c != '{')
    {
        return -1;
    }

    /* Read top-level key-value pairs. */
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
            version = (int)read_ulong(fp);
        }
        else if (strcmp(key, "master_name") == 0)
        {
            c = skip_ws(fp);
            if (c != '"')
            {
                return -1;
            }
            read_json_string(fp, table->master_name, HIGHSCORE_NAME_LEN);
        }
        else if (strcmp(key, "master_text") == 0)
        {
            c = skip_ws(fp);
            if (c != '"')
            {
                return -1;
            }
            read_json_string(fp, table->master_text, HIGHSCORE_NAME_LEN);
        }
        else if (strcmp(key, "entries") == 0)
        {
            /* Expect '['. */
            c = skip_ws(fp);
            if (c != '[')
            {
                return -1;
            }

            for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
            {
                c = skip_ws(fp);
                if (c == ']')
                {
                    break;
                }
                if (c == ',')
                {
                    c = skip_ws(fp);
                }
                if (c == ']')
                {
                    break;
                }
                if (c != '{')
                {
                    return -1;
                }

                highscore_entry_t *e = &table->entries[i];

                /* Read entry key-value pairs. */
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

                    char ekey[32];
                    if (read_json_string(fp, ekey, (int)sizeof(ekey)) < 0)
                    {
                        return -1;
                    }
                    if (skip_to_colon(fp) < 0)
                    {
                        return -1;
                    }

                    if (strcmp(ekey, "score") == 0)
                    {
                        e->score = read_ulong(fp);
                    }
                    else if (strcmp(ekey, "level") == 0)
                    {
                        e->level = read_ulong(fp);
                    }
                    else if (strcmp(ekey, "game_time") == 0)
                    {
                        e->game_time = read_ulong(fp);
                    }
                    else if (strcmp(ekey, "timestamp") == 0)
                    {
                        e->timestamp = read_ulong(fp);
                    }
                    else if (strcmp(ekey, "name") == 0)
                    {
                        c = skip_ws(fp);
                        if (c != '"')
                        {
                            return -1;
                        }
                        read_json_string(fp, e->name, HIGHSCORE_NAME_LEN);
                    }
                    else
                    {
                        /* Unknown key — skip to next , or }. */
                        if (skip_to(fp, ',') < 0)
                        {
                            return -1;
                        }
                    }
                }
            }

            /* Skip to end of array. */
            c = skip_ws(fp);
            if (c != ']')
            {
                /* Consume remaining entries (if > NUM_ENTRIES). */
                while (c != ']' && c != EOF)
                {
                    c = fgetc(fp);
                }
            }
        }
        else
        {
            /* Unknown top-level key — skip to next , or }. */
            /* Simple approach: skip to next comma or brace. */
            int depth = 0;
            for (;;)
            {
                c = fgetc(fp);
                if (c == EOF)
                {
                    return -1;
                }
                if (c == '{' || c == '[')
                {
                    depth++;
                }
                if (c == '}' || c == ']')
                {
                    if (depth == 0)
                    {
                        ungetc(c, fp);
                        break;
                    }
                    depth--;
                }
                if (c == ',' && depth == 0)
                {
                    break;
                }
            }
        }
    }

    if (version != HIGHSCORE_IO_VERSION)
    {
        return -2; /* version mismatch */
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

    /* Find last '/' and null-terminate there. */
    char *sep = strrchr(buf, '/');
    if (!sep || sep == buf)
    {
        return;
    }
    *sep = '\0';

    /* Try to create the directory (and parents). */
    /* Simple approach: try mkdir, if it fails try parent first. */
    struct stat st;
    if (stat(buf, &st) == 0)
    {
        return; /* already exists */
    }

    /* Recursively create parent. */
    ensure_parent_dir(buf);
    (void)mkdir(buf, 0755);
}

/* =========================================================================
 * Public API: I/O
 * ========================================================================= */

highscore_io_result_t highscore_io_read(const char *path, highscore_table_t *table)
{
    if (!path || !table)
    {
        return HIGHSCORE_IO_ERR_NULL;
    }

    highscore_io_init_table(table);

    FILE *fp = fopen(path, "r");
    if (!fp)
    {
        return HIGHSCORE_IO_ERR_OPEN;
    }

    int result = read_table_json(fp, table);
    fclose(fp);

    if (result == -2)
    {
        highscore_io_init_table(table);
        return HIGHSCORE_IO_ERR_VERSION;
    }
    if (result < 0)
    {
        highscore_io_init_table(table);
        return HIGHSCORE_IO_ERR_READ;
    }

    return HIGHSCORE_IO_OK;
}

highscore_io_result_t highscore_io_write(const char *path, const highscore_table_t *table)
{
    if (!path || !table)
    {
        return HIGHSCORE_IO_ERR_NULL;
    }

    ensure_parent_dir(path);

    /* Write to a temp file, then rename for atomicity. */
    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *fp = fopen(tmp_path, "w");
    if (!fp)
    {
        return HIGHSCORE_IO_ERR_OPEN;
    }

    /* Sort a copy before writing. */
    highscore_table_t sorted = *table;
    highscore_io_sort(&sorted);

    if (write_table_json(fp, &sorted) < 0)
    {
        fclose(fp);
        (void)remove(tmp_path);
        return HIGHSCORE_IO_ERR_WRITE;
    }

    if (fclose(fp) != 0)
    {
        (void)remove(tmp_path);
        return HIGHSCORE_IO_ERR_WRITE;
    }

    if (rename(tmp_path, path) != 0)
    {
        (void)remove(tmp_path);
        return HIGHSCORE_IO_ERR_RENAME;
    }

    return HIGHSCORE_IO_OK;
}

/* =========================================================================
 * Public API: Score management
 * ========================================================================= */

void highscore_io_sort(highscore_table_t *table)
{
    if (!table)
    {
        return;
    }

    /* Bubble sort (10 entries — not worth a fancier algorithm). */
    for (int i = 0; i < HIGHSCORE_NUM_ENTRIES - 1; i++)
    {
        for (int j = HIGHSCORE_NUM_ENTRIES - 1; j > i; j--)
        {
            if (table->entries[j].score > table->entries[j - 1].score)
            {
                highscore_entry_t tmp = table->entries[j - 1];
                table->entries[j - 1] = table->entries[j];
                table->entries[j] = tmp;
            }
        }
    }
}

highscore_io_result_t highscore_io_insert(highscore_table_t *table, unsigned long score,
                                          unsigned long level, unsigned long game_time,
                                          unsigned long timestamp, const char *name)
{
    if (!table || !name)
    {
        return HIGHSCORE_IO_ERR_NULL;
    }

    /* Find the insertion rank. */
    int rank = -1;
    for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        if (score > table->entries[i].score)
        {
            rank = i;
            break;
        }
    }

    if (rank < 0)
    {
        return HIGHSCORE_IO_ERR_NOT_RANKED;
    }

    /* Shift entries down from the bottom. */
    for (int i = HIGHSCORE_NUM_ENTRIES - 1; i > rank; i--)
    {
        table->entries[i] = table->entries[i - 1];
    }

    /* Insert the new entry. */
    highscore_entry_t *e = &table->entries[rank];
    e->score = score;
    e->level = level;
    e->game_time = game_time;
    e->timestamp = timestamp;
    strncpy(e->name, name, HIGHSCORE_NAME_LEN - 1);
    e->name[HIGHSCORE_NAME_LEN - 1] = '\0';

    /* Update master if this is the new #1. */
    if (rank == 0)
    {
        strncpy(table->master_name, name, HIGHSCORE_NAME_LEN - 1);
        table->master_name[HIGHSCORE_NAME_LEN - 1] = '\0';
    }

    return HIGHSCORE_IO_OK;
}

int highscore_io_get_ranking(const highscore_table_t *table, unsigned long score)
{
    if (!table)
    {
        return -1;
    }

    for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        if (score > table->entries[i].score)
        {
            return i + 1; /* 1-based rank */
        }
    }

    return -1;
}

/* =========================================================================
 * Public API: Initialization
 * ========================================================================= */

void highscore_io_init_table(highscore_table_t *table)
{
    if (!table)
    {
        return;
    }

    memset(table, 0, sizeof(*table));
    strncpy(table->master_text, "Anyone play this game?", HIGHSCORE_NAME_LEN - 1);
}
