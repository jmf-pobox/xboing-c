#ifndef SDL2_CLI_H
#define SDL2_CLI_H

/*
 * sdl2_cli.h — Command-line option parsing for SDL2-based XBoing.
 *
 * Replaces the legacy prefix-matching parser in init.c with
 * a standalone module.  Parses argv into a config struct; the caller wires
 * values into SDL2 subsystems (sdl2_loop, sdl2_audio, etc.).
 *
 * Drops X11-specific options (-display, -sync, -usedefcmap, -noicon) that
 * have no SDL2 equivalent.  All gameplay-affecting options are preserved.
 *
 * Pure C — no SDL2 dependency.  Fully testable.
 * See ADR-014 in docs/DESIGN.md for design rationale.
 */

#include <stdbool.h>

/* =========================================================================
 * Constants
 * ========================================================================= */

#define SDL2C_MAX_NICKNAME_LEN 20
#define SDL2C_MIN_SPEED 1
#define SDL2C_MAX_SPEED 9
#define SDL2C_DEFAULT_SPEED 5
#define SDL2C_MIN_LEVEL 1
#define SDL2C_MAX_LEVEL 80
#define SDL2C_DEFAULT_LEVEL 1
#define SDL2C_MIN_VOLUME 0
#define SDL2C_MAX_VOLUME 100

/* =========================================================================
 * Status codes
 * ========================================================================= */

typedef enum
{
    SDL2C_OK = 0,
    SDL2C_EXIT_HELP,    /* -help, -usage: print help and exit 0 */
    SDL2C_EXIT_VERSION, /* -version: print version and exit 0 */
    SDL2C_EXIT_SETUP,   /* -setup: print setup info and exit 0 */
    SDL2C_EXIT_SCORES,  /* -scores: print scores and exit 0 */
    SDL2C_ERR_NULL_ARG,
    SDL2C_ERR_MISSING_VALUE,  /* option requires argument */
    SDL2C_ERR_INVALID_VALUE,  /* argument out of range */
    SDL2C_ERR_UNKNOWN_OPTION, /* unrecognized flag */
} sdl2_cli_status_t;

/* =========================================================================
 * Parsed configuration
 * ========================================================================= */

typedef struct
{
    /* Gameplay options */
    int speed;       /* 1-9, default 5 */
    int start_level; /* 1-80, default 1 */
    bool use_keys;   /* false = mouse (default), true = keyboard */
    bool sfx;        /* true = SFX on (default), false = off */

    /* Audio options */
    bool sound;     /* false = no sound (default), true = enable */
    int max_volume; /* 0-100, default 0 (system default) */

    /* Player options */
    char nickname[SDL2C_MAX_NICKNAME_LEN + 1]; /* "" = use real name */
    bool debug;                                /* false = normal, true = debug mode */

    /* Display options */
    bool grab; /* false = no pointer grab (default), true = grab */
} sdl2_cli_config_t;

/* =========================================================================
 * API
 * ========================================================================= */

/*
 * Return a config struct populated with default values.
 */
sdl2_cli_config_t sdl2_cli_config_defaults(void);

/*
 * Parse command-line arguments into a config struct.
 *
 * argc/argv are the standard main() parameters.
 * config must point to an initialized (e.g., default) struct.
 *
 * Returns SDL2C_OK on success.
 * Returns SDL2C_EXIT_* if an informational flag was found (caller should
 * print the requested info and exit 0).
 * Returns SDL2C_ERR_* on error (bad_option stores the problematic argv
 * element if non-NULL).
 */
sdl2_cli_status_t sdl2_cli_parse(int argc, char *const argv[], sdl2_cli_config_t *config,
                                 const char **bad_option);

/* Return a human-readable string for a status code. */
const char *sdl2_cli_status_string(sdl2_cli_status_t status);

#endif /* SDL2_CLI_H */
