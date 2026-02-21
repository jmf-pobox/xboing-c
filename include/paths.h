#ifndef PATHS_H
#define PATHS_H

/*
 * XDG Base Directory path resolution — no X11, no SDL2.
 *
 * Centralizes all file path construction for levels, sounds, scores,
 * and save files.  Resolution follows the XDG Base Directory spec with
 * fallbacks to legacy env vars and CWD for development mode.
 *
 * All functions write into caller-provided buffers (no malloc, no
 * static buffers).  paths_init_explicit() accepts injected env values
 * for deterministic testing.
 */

#include <stddef.h>

/* Maximum number of colon-separated XDG_DATA_DIRS entries we track. */
#define PATHS_MAX_DATA_DIRS 8

/* Maximum path length for any single path buffer. */
#define PATHS_MAX_PATH 1024

typedef enum
{
    PATHS_OK = 0,       /* Success. */
    PATHS_NOT_FOUND,    /* File does not exist at any candidate location. */
    PATHS_TRUNCATED,    /* Result was truncated — buffer too small. */
    PATHS_NO_HOME       /* $HOME is unset or empty — cannot resolve paths. */
} paths_status_t;

/*
 * Opaque-ish config struct.  Exposed in the header so callers can
 * stack-allocate it.  Treat fields as read-only after init.
 */
typedef struct
{
    char home[PATHS_MAX_PATH];
    char xdg_data_home[PATHS_MAX_PATH];
    char xdg_config_home[PATHS_MAX_PATH];
    char xdg_data_dirs[PATHS_MAX_DATA_DIRS][PATHS_MAX_PATH];
    int xdg_data_dirs_count;

    /* Legacy env var overrides (empty string = not set). */
    char xboing_levels_dir[PATHS_MAX_PATH];
    char xboing_sound_dir[PATHS_MAX_PATH];
    char xboing_score_file[PATHS_MAX_PATH];
} paths_config_t;

/*
 * Initialize from the real environment (getenv).
 * Returns PATHS_NO_HOME if $HOME is unset or empty.
 */
paths_status_t paths_init(paths_config_t *cfg);

/*
 * Initialize with injected values.  Any parameter may be NULL (treated
 * as "not set").  This is the testing seam — no getenv() calls.
 *
 * xdg_data_dirs is colon-separated, matching the env var format.
 */
paths_status_t paths_init_explicit(paths_config_t *cfg, const char *home,
                                   const char *xdg_data_home, const char *xdg_config_home,
                                   const char *xdg_data_dirs, const char *xboing_levels,
                                   const char *xboing_sounds, const char *xboing_scores);

/* --- Read-only asset resolution (checks file existence) ------------------- */

/*
 * Resolve a level file.  filename is e.g. "level01.data".
 * Writes the full path into buf.  Returns PATHS_NOT_FOUND if no
 * candidate exists on disk.
 */
paths_status_t paths_level_file(const paths_config_t *cfg, const char *filename, char *buf,
                                size_t bufsize);

/*
 * Resolve a sound file.  name is e.g. "balllost" (no extension).
 * The ".au" suffix is appended automatically.
 * Writes the full path into buf.  Returns PATHS_NOT_FOUND if no
 * candidate exists on disk.
 */
paths_status_t paths_sound_file(const paths_config_t *cfg, const char *name, char *buf,
                                size_t bufsize);

/* --- Writable user state (constructs path, does NOT check existence) ------ */

/* Global high-score file (shared across users on multi-user installs). */
paths_status_t paths_score_file_global(const paths_config_t *cfg, char *buf, size_t bufsize);

/* Per-user personal score file. */
paths_status_t paths_score_file_personal(const paths_config_t *cfg, char *buf, size_t bufsize);

/* Save-game state: game info (ball count, lives, score, etc.). */
paths_status_t paths_save_info(const paths_config_t *cfg, char *buf, size_t bufsize);

/* Save-game state: level data. */
paths_status_t paths_save_level(const paths_config_t *cfg, char *buf, size_t bufsize);

/* --- Directory accessors (for -setup display, mkdir) ---------------------- */

/* Base levels directory (without trailing filename). */
paths_status_t paths_levels_dir(const paths_config_t *cfg, char *buf, size_t bufsize);

/* Base sounds directory (without trailing filename). */
paths_status_t paths_sounds_dir(const paths_config_t *cfg, char *buf, size_t bufsize);

/* User data directory ($XDG_DATA_HOME/xboing). */
paths_status_t paths_user_data_dir(const paths_config_t *cfg, char *buf, size_t bufsize);

#endif /* PATHS_H */
