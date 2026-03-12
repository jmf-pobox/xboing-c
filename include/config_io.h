/*
 * config_io.h — TOML-based user preferences I/O.
 *
 * Reads and writes user preferences as a minimal TOML file,
 * replacing the legacy approach of compile-time defines and
 * environment variables for persistent user settings.
 *
 * File location: XDG_CONFIG_HOME/xboing/config.toml
 *   (typically ~/.config/xboing/config.toml)
 *
 * File format (flat key = value, no tables):
 *   # XBoing configuration
 *   speed = 5
 *   start_level = 1
 *   control = "mouse"
 *   sfx = true
 *   sound = false
 *   max_volume = 0
 *   nickname = ""
 *
 * CLI flags override config file values at startup.
 *
 * Legacy source: init.c ParseCommandLine() (compile-time defaults + argv).
 * See ADR-034 in docs/DESIGN.md for design rationale.
 */

#ifndef CONFIG_IO_H
#define CONFIG_IO_H

#include <stdbool.h>

/* =========================================================================
 * Constants
 * ========================================================================= */

#define CONFIG_IO_VERSION 1
#define CONFIG_IO_MAX_NICKNAME_LEN 20

/* Default values — sound on at 80% volume. */
#define CONFIG_IO_DEFAULT_SPEED 5
#define CONFIG_IO_DEFAULT_START_LEVEL 1
#define CONFIG_IO_DEFAULT_MAX_VOLUME 80

/* =========================================================================
 * Types
 * ========================================================================= */

/* User preferences for persistent storage. */
typedef struct
{
    int speed;                                     /* 1-9, default 5 */
    int start_level;                               /* 1-80, default 1 */
    bool use_keys;                                 /* false=mouse, true=keyboard */
    bool sfx;                                      /* true=SFX on (default) */
    bool sound;                                    /* false=no sound (default) */
    int max_volume;                                /* 0-100, default 0 */
    char nickname[CONFIG_IO_MAX_NICKNAME_LEN + 1]; /* ""=use real name */
} config_data_t;

/* Result codes. */
typedef enum
{
    CONFIG_IO_OK = 0,
    CONFIG_IO_ERR_NULL,
    CONFIG_IO_ERR_OPEN,
    CONFIG_IO_ERR_READ,
    CONFIG_IO_ERR_WRITE,
    CONFIG_IO_ERR_RENAME,
} config_io_result_t;

/* =========================================================================
 * File I/O
 * ========================================================================= */

/*
 * Read user preferences from a TOML file.
 * On success, fills *data and returns CONFIG_IO_OK.
 * On failure, *data is set to defaults and an error code is returned.
 * Unknown keys are silently ignored (forward compatibility).
 */
config_io_result_t config_io_read(const char *path, config_data_t *data);

/*
 * Write user preferences to a TOML file (atomic: temp + rename).
 * Creates parent directories if needed.
 */
config_io_result_t config_io_write(const char *path, const config_data_t *data);

/*
 * Check if a config file exists at the given path.
 * Returns 1 if it exists, 0 otherwise.
 */
int config_io_exists(const char *path);

/*
 * Initialize a config_data_t to sensible defaults (new install).
 */
void config_io_init(config_data_t *data);

#endif /* CONFIG_IO_H */
