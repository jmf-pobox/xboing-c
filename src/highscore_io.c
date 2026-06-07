/*
 * highscore_io.c — JSON-based high score file I/O.
 *
 * See highscore_io.h for module overview.
 */

#include "highscore_io.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

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
                    "\"game_time\": %lu, \"timestamp\": %lu, "
                    "\"user_id\": %lu, \"name\": ",
                    e->score, e->level, e->game_time, e->timestamp, e->user_id) < 0)
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

            int array_closed = 0;
            for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
            {
                c = skip_ws(fp);
                if (c == ']')
                {
                    array_closed = 1;
                    break;
                }
                if (c == ',')
                {
                    c = skip_ws(fp);
                }
                if (c == ']')
                {
                    array_closed = 1;
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
                    else if (strcmp(ekey, "user_id") == 0)
                    {
                        e->user_id = read_ulong(fp);
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

            /* Skip to end of array only if the inner loop hit the
             * NUM_ENTRIES cap before seeing `]` — otherwise the inner
             * loop already consumed the closing bracket and another
             * skip_ws here would eat the surrounding `}` or trailing
             * key, silently destroying the parse for empty-array files. */
            if (!array_closed)
            {
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

    /* Write to a temp file, then rename for atomicity.  When invoked
     * under setgid-games (the multi-user global write path), the
     * directory is group-writable so a malicious group member could
     * pre-create scores.dat.tmp as a symlink pointing at an arbitrary
     * file the games group can touch — fopen("w") would follow it and
     * truncate the target.  Defenses: O_EXCL refuses to open an
     * existing path (races become a clear error rather than a
     * follow); O_NOFOLLOW refuses any symlink at the leaf.  If a
     * stale .tmp exists from a crashed prior run, unlink it first
     * (best-effort — same-uid can clean, cross-uid will fail at
     * O_EXCL and surface the problem). */
    char tmp_path[1024];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    (void)unlink(tmp_path);

    int tmp_fd = open(tmp_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0664);
    if (tmp_fd < 0)
    {
        return HIGHSCORE_IO_ERR_OPEN;
    }
    FILE *fp = fdopen(tmp_fd, "w");
    if (!fp)
    {
        close(tmp_fd);
        (void)unlink(tmp_path);
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

/*
 * In-place writer for the global score file.  Used by
 * highscore_io_insert_global_atomic to preserve the postinst-set
 * root:games inode ownership.  The temp+rename pattern in
 * highscore_io_write would otherwise create a new player-owned
 * inode, after which the player could edit the global leaderboard
 * directly outside the locked dedup path.
 *
 * Opens existing file only (no O_CREAT).  O_NOFOLLOW refuses
 * symlinks at the leaf.  ftruncates to 0 then writes the JSON.
 * Not crash-atomic against a half-write — but the caller holds
 * flock(LOCK_EX), so concurrent writers can't observe a torn
 * file, and a crash leaves a corrupt file that the next successful
 * write replaces.  Trade-off documented in ADR-041.
 */
static highscore_io_result_t write_table_inplace(const char *path, const highscore_table_t *table)
{
    int fd = open(path, O_WRONLY | O_NOFOLLOW);
    if (fd < 0)
    {
        return HIGHSCORE_IO_ERR_OPEN;
    }
    if (ftruncate(fd, 0) != 0)
    {
        close(fd);
        return HIGHSCORE_IO_ERR_WRITE;
    }
    FILE *fp = fdopen(fd, "w");
    if (!fp)
    {
        close(fd);
        return HIGHSCORE_IO_ERR_OPEN;
    }
    highscore_table_t sorted = *table;
    highscore_io_sort(&sorted);
    if (write_table_json(fp, &sorted) < 0)
    {
        fclose(fp);
        return HIGHSCORE_IO_ERR_WRITE;
    }
    fflush(fp);
    if (fsync(fileno(fp)) != 0)
    {
        fclose(fp);
        return HIGHSCORE_IO_ERR_WRITE;
    }
    if (fclose(fp) != 0)
    {
        return HIGHSCORE_IO_ERR_WRITE;
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

/* =========================================================================
 * Global atomic insert — read-modify-write under flock with uid dedup.
 * ========================================================================= */

highscore_io_result_t
highscore_io_insert_global_atomic(const char *path, unsigned long score, unsigned long level,
                                  unsigned long game_time, unsigned long timestamp,
                                  unsigned long user_id, const char *name, const char *master_text)
{
    if (!path || !name)
    {
        return HIGHSCORE_IO_ERR_NULL;
    }

    /* Do NOT ensure_parent_dir here.  The global score directory is
     * provisioned by debian/xboing.postinst as `root:games` mode 2755;
     * creating it on the fly as the calling user would land on mode
     * 0755 owned by user:user-primary-group, breaking the trust model
     * and locking out other users from a leaderboard the package
     * promised to keep shared.  Surface the missing directory as an
     * OPEN error instead — sysadmin can fix the install. */
    struct stat parent_st;
    char parent[1024];
    snprintf(parent, sizeof(parent), "%s", path);
    {
        char *slash = strrchr(parent, '/');
        if (slash && slash != parent)
        {
            *slash = '\0';
        }
        else
        {
            parent[0] = '\0';
        }
    }
    if (parent[0] != '\0' && (stat(parent, &parent_st) != 0 || !S_ISDIR(parent_st.st_mode)))
    {
        return HIGHSCORE_IO_ERR_OPEN;
    }

    /* Lock file lives next to the table file.  flock(LOCK_EX)
     * serializes concurrent writers (multiple users finishing games
     * at the same time).  O_NOFOLLOW refuses to open a symlink — the
     * global directory is group-writable (setgid games), so any
     * member of the games group could pre-create scores.dat.lock as
     * a symlink to elsewhere; without O_NOFOLLOW the setgid process
     * would happily follow it. */
    char lock_path[1024];
    snprintf(lock_path, sizeof(lock_path), "%s.lock", path);

    int lock_fd = open(lock_path, O_CREAT | O_RDWR | O_NOFOLLOW, 0664);
    if (lock_fd < 0)
    {
        return HIGHSCORE_IO_ERR_OPEN;
    }
    if (flock(lock_fd, LOCK_EX) != 0)
    {
        close(lock_fd);
        return HIGHSCORE_IO_ERR_OPEN;
    }

    /* Re-read from disk so we see any concurrent updates. */
    highscore_table_t table;
    highscore_io_result_t rd = highscore_io_read(path, &table);
    if (rd == HIGHSCORE_IO_ERR_VERSION)
    {
        /* Refuse to clobber a wrong-version file under the global lock —
         * the player would lose other users' scores.  Sysadmin must
         * resolve before any further global writes. */
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        fprintf(stderr,
                "xboing: global high score file %s has unsupported version; "
                "refusing to overwrite\n",
                path);
        return HIGHSCORE_IO_ERR_VERSION;
    }
    if (rd != HIGHSCORE_IO_OK)
    {
        /* File missing or unparseable — initialise an empty table.
         * Postinst seeds the file but a sysadmin may have wiped it. */
        highscore_io_init_table(&table);
    }

    /* Per-uid dedup (original/highscore.c:721-737).  If this user
     * already has an entry, keep whichever score is higher.  Walking
     * the array is fine — HIGHSCORE_NUM_ENTRIES is 10.
     *
     * Original assumes one entry per uid as an invariant.  A
     * hand-edited file can violate it.  Be defensive: find the
     * highest existing score for our uid, gate on it, then remove
     * EVERY entry for our uid before the standard rank insert.  Net
     * effect: post-insert the table holds exactly one entry per uid
     * regardless of pre-existing duplicates. */
    unsigned long existing_best = 0;
    int existing_count = 0;
    for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        if (table.entries[i].user_id == user_id && table.entries[i].score > 0)
        {
            existing_count++;
            if (table.entries[i].score > existing_best)
            {
                existing_best = table.entries[i].score;
            }
        }
    }
    if (existing_count > 0 && score <= existing_best)
    {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        return HIGHSCORE_IO_ERR_NOT_RANKED;
    }
    if (existing_count > 0)
    {
        /* Compact: copy non-our-uid entries, zero-fill the tail. */
        highscore_entry_t kept[HIGHSCORE_NUM_ENTRIES];
        memset(kept, 0, sizeof(kept));
        int n = 0;
        for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
        {
            if (table.entries[i].user_id != user_id)
            {
                kept[n++] = table.entries[i];
            }
        }
        memcpy(table.entries, kept, sizeof(table.entries));
    }

    /* Standard rank insert.  Use strict > — original/highscore.c:743
     * uses `score > ntohl(highScores[i].score)`; ties do NOT displace.
     * (The display-side highscore_io_get_ranking uses >= because
     * original/highscore.c:633 does too — different semantic.) */
    int rank = -1;
    for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        if (score > table.entries[i].score)
        {
            rank = i;
            break;
        }
    }
    if (rank < 0)
    {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        return HIGHSCORE_IO_ERR_NOT_RANKED;
    }
    for (int i = HIGHSCORE_NUM_ENTRIES - 1; i > rank; i--)
    {
        table.entries[i] = table.entries[i - 1];
    }
    highscore_entry_t *e = &table.entries[rank];
    e->score = score;
    e->level = level;
    e->game_time = game_time;
    e->timestamp = timestamp;
    e->user_id = user_id;
    strncpy(e->name, name, HIGHSCORE_NAME_LEN - 1);
    e->name[HIGHSCORE_NAME_LEN - 1] = '\0';

    /* New boing master — update master_name and master_text together
     * so they always describe the same person.  Original writes both
     * unconditionally at i==0 (highscore.c:744 ShiftScoresDown sets
     * name, :749 SetBoingMasterText sets text); the caller may pass
     * NULL/empty for master_text on a cancelled wisdom dialog, in
     * which case we use the default placeholder rather than leave the
     * previous master's quote attached to a new name. */
    if (rank == 0)
    {
        strncpy(table.master_name, name, HIGHSCORE_NAME_LEN - 1);
        table.master_name[HIGHSCORE_NAME_LEN - 1] = '\0';
        const char *text =
            (master_text && master_text[0] != '\0') ? master_text : "Anyone play this game?";
        strncpy(table.master_text, text, HIGHSCORE_NAME_LEN - 1);
        table.master_text[HIGHSCORE_NAME_LEN - 1] = '\0';
    }

    /* Use in-place write rather than temp+rename: the latter would
     * change the inode and leave the global file owned by the calling
     * user, after which they could edit the leaderboard directly.
     * See write_table_inplace's docstring and ADR-041. */
    highscore_io_result_t wr = write_table_inplace(path, &table);

    flock(lock_fd, LOCK_UN);
    close(lock_fd);

    return wr;
}

highscore_io_result_t highscore_io_insert(highscore_table_t *table, unsigned long score,
                                          unsigned long level, unsigned long game_time,
                                          unsigned long timestamp, const char *name)
{
    if (!table || !name)
    {
        return HIGHSCORE_IO_ERR_NULL;
    }

    /* Find the insertion rank.  Use strict > — original/highscore.c:777
     * uses `score > ntohl(highScores[i].score)` for the personal insert,
     * so ties do not displace. */
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
    e->user_id = 0; /* personal-table entries don't track uid */
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
        /* Match original/highscore.c:633 — ties rank ahead. */
        if (score >= table->entries[i].score)
        {
            return i + 1; /* 1-based rank */
        }
    }

    return -1;
}

int highscore_io_would_be_global_master(const highscore_table_t *table, unsigned long score,
                                        unsigned long user_id)
{
    if (!table)
    {
        return 0;
    }

    /* Per-uid dedup: the locked insert returns NOT_RANKED whenever
     * new_score <= MAX(our existing scores).  Walk every entry rather
     * than breaking at the first match — a hand-edited file may carry
     * duplicates, and the insert dedups against the highest of them. */
    unsigned long existing_best = 0;
    for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        if (table->entries[i].user_id == user_id && table->entries[i].score > existing_best)
        {
            existing_best = table->entries[i].score;
        }
    }
    if (existing_best > 0 && score <= existing_best)
    {
        return 0;
    }

    /* Find the top score from users other than us.  The atomic insert
     * uses strict > (matching original/highscore.c:743), so we must
     * strictly beat top_other to land at rank 0. */
    unsigned long top_other = 0;
    for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        if (table->entries[i].user_id == user_id)
        {
            continue;
        }
        if (table->entries[i].score > top_other)
        {
            top_other = table->entries[i].score;
        }
    }
    return score > top_other ? 1 : 0;
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
