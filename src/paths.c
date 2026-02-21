/*
 * paths.c — XDG Base Directory path resolution.
 *
 * Pure C, no X11 or SDL2 dependency.  All functions write into
 * caller-provided buffers.  See include/paths.h for API docs.
 */

#include "paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* --- Internal helpers ----------------------------------------------------- */

/*
 * Safe string copy into a fixed buffer.  Returns 0 on success,
 * -1 if the source was truncated.
 */
static int safe_copy(char *dst, size_t dstsize, const char *src)
{
    if (dstsize == 0)
        return -1;
    if (src == NULL)
    {
        dst[0] = '\0';
        return 0;
    }
    size_t len = strlen(src);
    if (len >= dstsize)
    {
        memcpy(dst, src, dstsize - 1);
        dst[dstsize - 1] = '\0';
        return -1;
    }
    memcpy(dst, src, len + 1);
    return 0;
}

/*
 * Build a path from up to 4 segments: base/a/b/c.
 * base must be non-NULL.  Segments a, b, and c may be NULL (skipped).
 * Returns PATHS_OK or PATHS_TRUNCATED.
 */
static paths_status_t build_path(char *buf, size_t bufsize, const char *base, const char *a,
                                 const char *b, const char *c)
{
    int n = snprintf(buf, bufsize, "%s%s%s%s%s%s%s", base, a ? "/" : "", a ? a : "", b ? "/" : "",
                     b ? b : "", c ? "/" : "", c ? c : "");
    if (n < 0 || (size_t)n >= bufsize)
        return PATHS_TRUNCATED;
    return PATHS_OK;
}

/* Return non-zero if the path exists (regular file or directory). */
static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/*
 * Strip a trailing slash from str in-place, unless str is "/" or empty.
 */
static void strip_trailing_slash(char *str)
{
    size_t len = strlen(str);
    if (len > 1 && str[len - 1] == '/')
        str[len - 1] = '\0';
}

/*
 * Parse a colon-separated string into the cfg->xdg_data_dirs array.
 * Stops at PATHS_MAX_DATA_DIRS entries.
 */
static void parse_data_dirs(paths_config_t *cfg, const char *dirs)
{
    cfg->xdg_data_dirs_count = 0;
    if (dirs == NULL || dirs[0] == '\0')
        return;

    /* Work on a local copy so we don't modify the input. */
    char tmp[PATHS_MAX_PATH * PATHS_MAX_DATA_DIRS];
    size_t len = strlen(dirs);
    if (len >= sizeof(tmp))
        len = sizeof(tmp) - 1;
    memcpy(tmp, dirs, len);
    tmp[len] = '\0';

    char *saveptr = NULL;
    char *tok = strtok_r(tmp, ":", &saveptr);
    while (tok != NULL && cfg->xdg_data_dirs_count < PATHS_MAX_DATA_DIRS)
    {
        if (tok[0] != '\0')
        {
            safe_copy(cfg->xdg_data_dirs[cfg->xdg_data_dirs_count], PATHS_MAX_PATH, tok);
            strip_trailing_slash(cfg->xdg_data_dirs[cfg->xdg_data_dirs_count]);
            cfg->xdg_data_dirs_count++;
        }
        tok = strtok_r(NULL, ":", &saveptr);
    }
}

/* --- Public API ----------------------------------------------------------- */

paths_status_t paths_init(paths_config_t *cfg)
{
    return paths_init_explicit(cfg, getenv("HOME"), getenv("XDG_DATA_HOME"),
                               getenv("XDG_CONFIG_HOME"), getenv("XDG_DATA_DIRS"),
                               getenv("XBOING_LEVELS_DIR"), getenv("XBOING_SOUND_DIR"),
                               getenv("XBOING_SCORE_FILE"));
}

paths_status_t paths_init_explicit(paths_config_t *cfg, const char *home,
                                   const char *xdg_data_home, const char *xdg_config_home,
                                   const char *xdg_data_dirs, const char *xboing_levels,
                                   const char *xboing_sounds, const char *xboing_scores)
{
    memset(cfg, 0, sizeof(*cfg));

    if (home == NULL || home[0] == '\0')
        return PATHS_NO_HOME;

    if (safe_copy(cfg->home, PATHS_MAX_PATH, home) != 0)
        return PATHS_TRUNCATED;
    strip_trailing_slash(cfg->home);

    /* XDG_DATA_HOME: default $HOME/.local/share */
    if (xdg_data_home != NULL && xdg_data_home[0] != '\0')
    {
        if (safe_copy(cfg->xdg_data_home, PATHS_MAX_PATH, xdg_data_home) != 0)
            return PATHS_TRUNCATED;
    }
    else
    {
        int n = snprintf(cfg->xdg_data_home, PATHS_MAX_PATH, "%s/.local/share", cfg->home);
        if (n < 0 || (size_t)n >= PATHS_MAX_PATH)
            return PATHS_TRUNCATED;
    }
    strip_trailing_slash(cfg->xdg_data_home);

    /* XDG_CONFIG_HOME: default $HOME/.config */
    if (xdg_config_home != NULL && xdg_config_home[0] != '\0')
    {
        if (safe_copy(cfg->xdg_config_home, PATHS_MAX_PATH, xdg_config_home) != 0)
            return PATHS_TRUNCATED;
    }
    else
    {
        int n = snprintf(cfg->xdg_config_home, PATHS_MAX_PATH, "%s/.config", cfg->home);
        if (n < 0 || (size_t)n >= PATHS_MAX_PATH)
            return PATHS_TRUNCATED;
    }
    strip_trailing_slash(cfg->xdg_config_home);

    /* XDG_DATA_DIRS: default /usr/local/share:/usr/share */
    if (xdg_data_dirs != NULL && xdg_data_dirs[0] != '\0')
    {
        parse_data_dirs(cfg, xdg_data_dirs);
    }
    else
    {
        parse_data_dirs(cfg, "/usr/local/share:/usr/share");
    }

    /* Legacy env var overrides. */
    if (xboing_levels != NULL && xboing_levels[0] != '\0')
    {
        if (safe_copy(cfg->xboing_levels_dir, PATHS_MAX_PATH, xboing_levels) != 0)
            return PATHS_TRUNCATED;
        strip_trailing_slash(cfg->xboing_levels_dir);
    }
    if (xboing_sounds != NULL && xboing_sounds[0] != '\0')
    {
        if (safe_copy(cfg->xboing_sound_dir, PATHS_MAX_PATH, xboing_sounds) != 0)
            return PATHS_TRUNCATED;
        strip_trailing_slash(cfg->xboing_sound_dir);
    }
    if (xboing_scores != NULL && xboing_scores[0] != '\0')
    {
        if (safe_copy(cfg->xboing_score_file, PATHS_MAX_PATH, xboing_scores) != 0)
            return PATHS_TRUNCATED;
    }

    return PATHS_OK;
}

/* --- Read-only asset resolution ------------------------------------------- */

/*
 * Try candidates in order for a read-only asset file.
 *
 * subdir is "levels" or "sounds".  filename is the leaf.
 * legacy_dir is the override env var value (empty = not set).
 */
static paths_status_t resolve_asset(const paths_config_t *cfg, const char *subdir,
                                    const char *filename, const char *legacy_dir, char *buf,
                                    size_t bufsize)
{
    if (filename == NULL || filename[0] == '\0')
        return PATHS_NOT_FOUND;

    paths_status_t st;

    /* 1. Legacy env var override. */
    if (legacy_dir[0] != '\0')
    {
        st = build_path(buf, bufsize, legacy_dir, filename, NULL, NULL);
        if (st == PATHS_TRUNCATED)
            return PATHS_TRUNCATED;
        if (file_exists(buf))
            return PATHS_OK;
    }

    /* 2. XDG_DATA_DIRS search. */
    for (int i = 0; i < cfg->xdg_data_dirs_count; i++)
    {
        st = build_path(buf, bufsize, cfg->xdg_data_dirs[i], "xboing", subdir, filename);
        if (st == PATHS_TRUNCATED)
            return PATHS_TRUNCATED;
        if (file_exists(buf))
            return PATHS_OK;
    }

    /* 3. XDG_DATA_HOME. */
    st = build_path(buf, bufsize, cfg->xdg_data_home, "xboing", subdir, filename);
    if (st == PATHS_TRUNCATED)
        return PATHS_TRUNCATED;
    if (file_exists(buf))
        return PATHS_OK;

    /* 4. CWD fallback (development mode). */
    st = build_path(buf, bufsize, subdir, filename, NULL, NULL);
    if (st == PATHS_TRUNCATED)
        return PATHS_TRUNCATED;
    if (file_exists(buf))
        return PATHS_OK;

    return PATHS_NOT_FOUND;
}

paths_status_t paths_level_file(const paths_config_t *cfg, const char *filename, char *buf,
                                size_t bufsize)
{
    return resolve_asset(cfg, "levels", filename, cfg->xboing_levels_dir, buf, bufsize);
}

paths_status_t paths_sound_file(const paths_config_t *cfg, const char *name, char *buf,
                                size_t bufsize)
{
    if (name == NULL || name[0] == '\0')
        return PATHS_NOT_FOUND;

    /* Append ".au" extension. */
    char filename[PATHS_MAX_PATH];
    int n = snprintf(filename, sizeof(filename), "%s.au", name);
    if (n < 0 || (size_t)n >= sizeof(filename))
        return PATHS_TRUNCATED;

    return resolve_asset(cfg, "sounds", filename, cfg->xboing_sound_dir, buf, bufsize);
}

/* --- Writable user state -------------------------------------------------- */

/*
 * Build a path under $XDG_DATA_HOME/xboing/<leaf>.
 */
static paths_status_t xdg_user_path(const paths_config_t *cfg, const char *leaf, char *buf,
                                    size_t bufsize)
{
    return build_path(buf, bufsize, cfg->xdg_data_home, "xboing", leaf, NULL);
}

paths_status_t paths_score_file_global(const paths_config_t *cfg, char *buf, size_t bufsize)
{
    /* 1. Legacy env var override. */
    if (cfg->xboing_score_file[0] != '\0')
    {
        if (safe_copy(buf, bufsize, cfg->xboing_score_file) != 0)
            return PATHS_TRUNCATED;
        return PATHS_OK;
    }

    /* 2. If a legacy file exists on disk, use it (migration compat). */
    paths_status_t st = build_path(buf, bufsize, cfg->home, ".xboing.scr", NULL, NULL);
    if (st == PATHS_TRUNCATED)
        return PATHS_TRUNCATED;
    if (file_exists(buf))
        return PATHS_OK;

    /* 3. Default to XDG for new installs: XDG_DATA_HOME/xboing/scores.dat */
    return xdg_user_path(cfg, "scores.dat", buf, bufsize);
}

paths_status_t paths_score_file_personal(const paths_config_t *cfg, char *buf, size_t bufsize)
{
    /* 1. If a legacy file exists on disk, use it (migration compat). */
    paths_status_t st = build_path(buf, bufsize, cfg->home, ".xboing-scores", NULL, NULL);
    if (st == PATHS_TRUNCATED)
        return PATHS_TRUNCATED;
    if (file_exists(buf))
        return PATHS_OK;

    /* 2. Default to XDG for new installs: XDG_DATA_HOME/xboing/personal-scores.dat */
    return xdg_user_path(cfg, "personal-scores.dat", buf, bufsize);
}

paths_status_t paths_save_info(const paths_config_t *cfg, char *buf, size_t bufsize)
{
    /* 1. If a legacy file exists on disk, use it (migration compat). */
    paths_status_t st = build_path(buf, bufsize, cfg->home, ".xboing-savinf", NULL, NULL);
    if (st == PATHS_TRUNCATED)
        return PATHS_TRUNCATED;
    if (file_exists(buf))
        return PATHS_OK;

    /* 2. Default to XDG for new installs: XDG_DATA_HOME/xboing/save-info.dat */
    return xdg_user_path(cfg, "save-info.dat", buf, bufsize);
}

paths_status_t paths_save_level(const paths_config_t *cfg, char *buf, size_t bufsize)
{
    /* 1. If a legacy file exists on disk, use it (migration compat). */
    paths_status_t st = build_path(buf, bufsize, cfg->home, ".xboing-savlev", NULL, NULL);
    if (st == PATHS_TRUNCATED)
        return PATHS_TRUNCATED;
    if (file_exists(buf))
        return PATHS_OK;

    /* 2. Default to XDG for new installs: XDG_DATA_HOME/xboing/save-level.dat */
    return xdg_user_path(cfg, "save-level.dat", buf, bufsize);
}

/* --- Directory accessors -------------------------------------------------- */

paths_status_t paths_levels_dir(const paths_config_t *cfg, char *buf, size_t bufsize)
{
    /* Legacy override first. */
    if (cfg->xboing_levels_dir[0] != '\0')
    {
        if (safe_copy(buf, bufsize, cfg->xboing_levels_dir) != 0)
            return PATHS_TRUNCATED;
        return PATHS_OK;
    }

    /* CWD fallback — matches development mode. */
    if (safe_copy(buf, bufsize, "levels") != 0)
        return PATHS_TRUNCATED;
    return PATHS_OK;
}

paths_status_t paths_sounds_dir(const paths_config_t *cfg, char *buf, size_t bufsize)
{
    /* Legacy override first. */
    if (cfg->xboing_sound_dir[0] != '\0')
    {
        if (safe_copy(buf, bufsize, cfg->xboing_sound_dir) != 0)
            return PATHS_TRUNCATED;
        return PATHS_OK;
    }

    /* CWD fallback — matches development mode. */
    if (safe_copy(buf, bufsize, "sounds") != 0)
        return PATHS_TRUNCATED;
    return PATHS_OK;
}

paths_status_t paths_user_data_dir(const paths_config_t *cfg, char *buf, size_t bufsize)
{
    return build_path(buf, bufsize, cfg->xdg_data_home, "xboing", NULL, NULL);
}
