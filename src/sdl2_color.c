/*
 * sdl2_color.c — SDL2 RGBA color system.
 *
 * See include/sdl2_color.h for API documentation.
 * See ADR-007 in docs/DESIGN.md for design rationale.
 */

#include "sdl2_color.h"

#include <ctype.h>
#include <stdbool.h>
#include <string.h>

/* =========================================================================
 * Color data — X11 color database values
 * ========================================================================= */

/*
 * Named colors matching the X11 rgb.txt entries used by the legacy game.
 * Values verified against /usr/share/X11/rgb.txt.
 */
static const SDL_Color named_colors[SDL2C_COLOR_COUNT] = {
    [SDL2C_RED] = {255, 0, 0, 255},       /* X11 "red" */
    [SDL2C_TAN] = {210, 180, 140, 255},   /* X11 "tan" */
    [SDL2C_YELLOW] = {255, 255, 0, 255},  /* X11 "yellow" */
    [SDL2C_GREEN] = {0, 255, 0, 255},     /* X11 "green" (NOT CSS #008000) */
    [SDL2C_WHITE] = {255, 255, 255, 255}, /* X11 "white" */
    [SDL2C_BLACK] = {0, 0, 0, 255},       /* X11 "black" */
    [SDL2C_BLUE] = {0, 0, 255, 255},      /* X11 "blue" */
    [SDL2C_PURPLE] = {160, 32, 240, 255}, /* X11 "purple" */
};

static const char *const color_names[SDL2C_COLOR_COUNT] = {
    [SDL2C_RED] = "red",     [SDL2C_TAN] = "tan",       [SDL2C_YELLOW] = "yellow",
    [SDL2C_GREEN] = "green", [SDL2C_WHITE] = "white",   [SDL2C_BLACK] = "black",
    [SDL2C_BLUE] = "blue",   [SDL2C_PURPLE] = "purple",
};

/*
 * Red gradient: brightest (#f00) to darkest (#300).
 * X11 3-digit hex #RGB expands to #RRGGBB by repeating each digit.
 * Matches legacy reds[7] from init.c:201-207.
 */
static const SDL_Color red_gradient[SDL2C_GRADIENT_STEPS] = {
    {0xFF, 0x00, 0x00, 0xFF}, /* #f00 */
    {0xDD, 0x00, 0x00, 0xFF}, /* #d00 */
    {0xBB, 0x00, 0x00, 0xFF}, /* #b00 */
    {0x99, 0x00, 0x00, 0xFF}, /* #900 */
    {0x77, 0x00, 0x00, 0xFF}, /* #700 */
    {0x55, 0x00, 0x00, 0xFF}, /* #500 */
    {0x33, 0x00, 0x00, 0xFF}, /* #300 */
};

/*
 * Green gradient: brightest (#0f0) to darkest (#030).
 * Matches legacy greens[7] from init.c:210-216.
 */
static const SDL_Color green_gradient[SDL2C_GRADIENT_STEPS] = {
    {0x00, 0xFF, 0x00, 0xFF}, /* #0f0 */
    {0x00, 0xDD, 0x00, 0xFF}, /* #0d0 */
    {0x00, 0xBB, 0x00, 0xFF}, /* #0b0 */
    {0x00, 0x99, 0x00, 0xFF}, /* #090 */
    {0x00, 0x77, 0x00, 0xFF}, /* #070 */
    {0x00, 0x55, 0x00, 0xFF}, /* #050 */
    {0x00, 0x33, 0x00, 0xFF}, /* #030 */
};

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/*
 * Case-insensitive string comparison.
 */
static bool str_eq_nocase(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0')
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
        {
            return false;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/*
 * Parse a single hex digit (0-9, a-f, A-F).
 * Returns -1 on invalid input.
 */
static int parse_hex_digit(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F')
    {
        return 10 + (c - 'A');
    }
    return -1;
}

/*
 * Parse a 3-digit hex color "#RGB" into an SDL_Color.
 * X11 expands each digit by repeating: #RGB → #RRGGBB.
 * Returns true on success.
 */
static bool parse_hex3(const char *str, SDL_Color *color)
{
    if (str == NULL || str[0] != '#' || strlen(str) != 4)
    {
        return false;
    }

    int r = parse_hex_digit(str[1]);
    int g = parse_hex_digit(str[2]);
    int b = parse_hex_digit(str[3]);

    if (r < 0 || g < 0 || b < 0)
    {
        return false;
    }

    /* X11 repeats each digit: #RGB → R*17, G*17, B*17 */
    color->r = (Uint8)(r * 17);
    color->g = (Uint8)(g * 17);
    color->b = (Uint8)(b * 17);
    color->a = 255;
    return true;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

SDL_Color sdl2_color_get(sdl2_color_id_t id)
{
    if (id >= 0 && id < SDL2C_COLOR_COUNT)
    {
        return named_colors[id];
    }
    return named_colors[SDL2C_BLACK];
}

SDL_Color sdl2_color_red_gradient(int index)
{
    if (index >= 0 && index < SDL2C_GRADIENT_STEPS)
    {
        return red_gradient[index];
    }
    return named_colors[SDL2C_BLACK];
}

SDL_Color sdl2_color_green_gradient(int index)
{
    if (index >= 0 && index < SDL2C_GRADIENT_STEPS)
    {
        return green_gradient[index];
    }
    return named_colors[SDL2C_BLACK];
}

bool sdl2_color_by_name(const char *name, SDL_Color *color)
{
    if (name == NULL || color == NULL)
    {
        return false;
    }

    /* Try hex format first. */
    if (name[0] == '#')
    {
        return parse_hex3(name, color);
    }

    /* Try named colors. */
    for (int i = 0; i < SDL2C_COLOR_COUNT; i++)
    {
        if (str_eq_nocase(name, color_names[i]))
        {
            *color = named_colors[i];
            return true;
        }
    }

    return false;
}

const char *sdl2_color_name(sdl2_color_id_t id)
{
    if (id >= 0 && id < SDL2C_COLOR_COUNT)
    {
        return color_names[id];
    }
    return "unknown";
}
