/*
 * sdl2_cli.c — Command-line option parsing for SDL2-based XBoing.
 *
 * See include/sdl2_cli.h for API documentation.
 * See ADR-014 in docs/DESIGN.md for design rationale.
 */

#include "sdl2_cli.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Option matching
 * ========================================================================= */

/*
 * Exact, case-sensitive option match.
 *
 * Returns true if and only if `arg` and `option` are identical strings.
 * This is stricter than the legacy compareArgument() in init.c, which
 * allowed prefix matching via strncmp().  Exact match avoids ambiguity.
 */
static bool match_option(const char *arg, const char *option)
{
    if (arg == NULL || option == NULL)
    {
        return false;
    }
    return strcmp(arg, option) == 0;
}

/*
 * Result of parse_int_arg: missing (argv exhausted), invalid (present but
 * not a valid integer), or ok (parsed successfully).
 */
typedef enum
{
    PARSE_INT_OK,
    PARSE_INT_MISSING,
    PARSE_INT_INVALID
} parse_int_result_t;

/*
 * Try to consume the next argv element as an integer value.
 * Returns PARSE_INT_MISSING if no argument follows, PARSE_INT_INVALID if
 * the argument is not a valid integer (or overflows), PARSE_INT_OK on
 * success (writes *value).
 */
// cppcheck-suppress constParameter
static parse_int_result_t parse_int_arg(int argc, char *const argv[], int *i, int *value)
{
    if (*i + 1 >= argc)
    {
        return PARSE_INT_MISSING;
    }

    (*i)++;
    char *end = NULL;
    errno = 0;
    long v = strtol(argv[*i], &end, 10);

    if (end == argv[*i] || *end != '\0')
    {
        return PARSE_INT_INVALID;
    }
    if (errno == ERANGE || v < INT_MIN || v > INT_MAX)
    {
        return PARSE_INT_INVALID;
    }

    *value = (int)v;
    return PARSE_INT_OK;
}

/*
 * Try to consume the next argv element as a string value.
 * Returns true and writes *value on success.
 */
// cppcheck-suppress constParameter
static bool parse_str_arg(int argc, char *const argv[], int *i, const char **value)
{
    if (*i + 1 >= argc)
    {
        return false;
    }

    (*i)++;
    *value = argv[*i];
    return true;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

sdl2_cli_config_t sdl2_cli_config_defaults(void)
{
    sdl2_cli_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.speed = SDL2C_DEFAULT_SPEED;
    cfg.start_level = SDL2C_DEFAULT_LEVEL;
    cfg.use_keys = false;
    cfg.sfx = true;
    cfg.sound = true;
    cfg.max_volume = 80;
    cfg.debug = false;
    cfg.grab = false;
    return cfg;
}

sdl2_cli_status_t sdl2_cli_parse(int argc, char *const argv[], sdl2_cli_config_t *config,
                                 const char **bad_option)
{
    if (config == NULL)
    {
        return SDL2C_ERR_NULL_ARG;
    }

    if (bad_option != NULL)
    {
        *bad_option = NULL;
    }

    for (int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];

        /* All options must start with '-'. */
        if (arg[0] != '-')
        {
            if (bad_option != NULL)
            {
                *bad_option = arg;
            }
            return SDL2C_ERR_UNKNOWN_OPTION;
        }

        /* Informational flags — caller prints and exits 0. */
        if (match_option(arg, "-help") || match_option(arg, "-usage"))
        {
            return SDL2C_EXIT_HELP;
        }
        if (match_option(arg, "-version"))
        {
            return SDL2C_EXIT_VERSION;
        }
        if (match_option(arg, "-setup"))
        {
            return SDL2C_EXIT_SETUP;
        }
        if (match_option(arg, "-scores"))
        {
            return SDL2C_EXIT_SCORES;
        }

        /* Boolean flags. */
        if (match_option(arg, "-debug"))
        {
            config->debug = true;
            continue;
        }
        if (match_option(arg, "-keys"))
        {
            config->use_keys = true;
            continue;
        }
        if (match_option(arg, "-sound"))
        {
            config->sound = true;
            continue;
        }
        if (match_option(arg, "-nosound"))
        {
            config->sound = false;
            continue;
        }
        if (match_option(arg, "-nosfx"))
        {
            config->sfx = false;
            continue;
        }
        if (match_option(arg, "-grab"))
        {
            config->grab = true;
            continue;
        }

        /* Options with integer arguments. */
        if (match_option(arg, "-speed"))
        {
            int val = 0;
            parse_int_result_t r = parse_int_arg(argc, argv, &i, &val);
            if (r == PARSE_INT_MISSING)
            {
                if (bad_option != NULL)
                {
                    *bad_option = arg;
                }
                return SDL2C_ERR_MISSING_VALUE;
            }
            if (r == PARSE_INT_INVALID || val < SDL2C_MIN_SPEED || val > SDL2C_MAX_SPEED)
            {
                if (bad_option != NULL)
                {
                    *bad_option = arg;
                }
                return SDL2C_ERR_INVALID_VALUE;
            }
            config->speed = val;
            continue;
        }

        if (match_option(arg, "-startlevel"))
        {
            int val = 0;
            parse_int_result_t r = parse_int_arg(argc, argv, &i, &val);
            if (r == PARSE_INT_MISSING)
            {
                if (bad_option != NULL)
                {
                    *bad_option = arg;
                }
                return SDL2C_ERR_MISSING_VALUE;
            }
            if (r == PARSE_INT_INVALID || val < SDL2C_MIN_LEVEL || val > SDL2C_MAX_LEVEL)
            {
                if (bad_option != NULL)
                {
                    *bad_option = arg;
                }
                return SDL2C_ERR_INVALID_VALUE;
            }
            config->start_level = val;
            continue;
        }

        if (match_option(arg, "-maxvol"))
        {
            int val = 0;
            parse_int_result_t r = parse_int_arg(argc, argv, &i, &val);
            if (r == PARSE_INT_MISSING)
            {
                if (bad_option != NULL)
                {
                    *bad_option = arg;
                }
                return SDL2C_ERR_MISSING_VALUE;
            }
            if (r == PARSE_INT_INVALID || val < SDL2C_MIN_VOLUME || val > SDL2C_MAX_VOLUME)
            {
                if (bad_option != NULL)
                {
                    *bad_option = arg;
                }
                return SDL2C_ERR_INVALID_VALUE;
            }
            config->max_volume = val;
            continue;
        }

        /* Option with string argument. */
        if (match_option(arg, "-nickname"))
        {
            const char *name = NULL;
            if (!parse_str_arg(argc, argv, &i, &name))
            {
                if (bad_option != NULL)
                {
                    *bad_option = arg;
                }
                return SDL2C_ERR_MISSING_VALUE;
            }

            /* Truncate to max length, matching legacy behavior. */
            size_t len = strlen(name);
            if (len > SDL2C_MAX_NICKNAME_LEN)
            {
                len = SDL2C_MAX_NICKNAME_LEN;
            }
            memcpy(config->nickname, name, len);
            config->nickname[len] = '\0';
            continue;
        }

        /* Unknown option. */
        if (bad_option != NULL)
        {
            *bad_option = arg;
        }
        return SDL2C_ERR_UNKNOWN_OPTION;
    }

    return SDL2C_OK;
}

const char *sdl2_cli_status_string(sdl2_cli_status_t status)
{
    switch (status)
    {
        case SDL2C_OK:
            return "OK";
        case SDL2C_EXIT_HELP:
            return "help requested";
        case SDL2C_EXIT_VERSION:
            return "version requested";
        case SDL2C_EXIT_SETUP:
            return "setup info requested";
        case SDL2C_EXIT_SCORES:
            return "scores requested";
        case SDL2C_ERR_NULL_ARG:
            return "NULL argument";
        case SDL2C_ERR_MISSING_VALUE:
            return "option requires a value";
        case SDL2C_ERR_INVALID_VALUE:
            return "value out of range";
        case SDL2C_ERR_UNKNOWN_OPTION:
            return "unknown option";
    }
    return "unknown status";
}
