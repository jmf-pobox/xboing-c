/*
 * test_sdl2_color.c — Unit tests for SDL2 RGBA color system.
 *
 * Bead xboing-oaa.4: SDL2 RGBA color system.
 *
 * Pure data tests — no SDL video driver needed.
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include <cmocka.h>

#include "sdl2_color.h"

/* =========================================================================
 * Helpers
 * ========================================================================= */

static void assert_color_eq(SDL_Color actual, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    assert_int_equal(actual.r, r);
    assert_int_equal(actual.g, g);
    assert_int_equal(actual.b, b);
    assert_int_equal(actual.a, a);
}

/* =========================================================================
 * Group 1: Named color lookup by enum
 * ========================================================================= */

/* TC-01: All 8 named colors have correct RGBA values from X11 rgb.txt. */
static void test_named_colors_x11_values(void **state)
{
    (void)state;
    assert_color_eq(sdl2_color_get(SDL2C_RED), 255, 0, 0, 255);
    assert_color_eq(sdl2_color_get(SDL2C_TAN), 210, 180, 140, 255);
    assert_color_eq(sdl2_color_get(SDL2C_YELLOW), 255, 255, 0, 255);
    assert_color_eq(sdl2_color_get(SDL2C_GREEN), 0, 255, 0, 255);
    assert_color_eq(sdl2_color_get(SDL2C_WHITE), 255, 255, 255, 255);
    assert_color_eq(sdl2_color_get(SDL2C_BLACK), 0, 0, 0, 255);
    assert_color_eq(sdl2_color_get(SDL2C_BLUE), 0, 0, 255, 255);
    assert_color_eq(sdl2_color_get(SDL2C_PURPLE), 160, 32, 240, 255);
}

/* TC-02: All named colors have alpha = 255 (fully opaque). */
static void test_named_colors_opaque(void **state)
{
    (void)state;
    for (int i = 0; i < SDL2C_COLOR_COUNT; i++)
    {
        SDL_Color c = sdl2_color_get((sdl2_color_id_t)i);
        assert_int_equal(c.a, 255);
    }
}

/* TC-03: Out-of-range enum returns black. */
static void test_get_out_of_range(void **state)
{
    (void)state;
    assert_color_eq(sdl2_color_get(SDL2C_COLOR_COUNT), 0, 0, 0, 255);
    assert_color_eq(sdl2_color_get((sdl2_color_id_t)-1), 0, 0, 0, 255);
    assert_color_eq(sdl2_color_get((sdl2_color_id_t)99), 0, 0, 0, 255);
}

/* =========================================================================
 * Group 2: Red gradient array
 * ========================================================================= */

/* TC-04: Red gradient matches legacy hex values from init.c. */
static void test_red_gradient_values(void **state)
{
    (void)state;
    assert_color_eq(sdl2_color_red_gradient(0), 0xFF, 0x00, 0x00, 0xFF); /* #f00 */
    assert_color_eq(sdl2_color_red_gradient(1), 0xDD, 0x00, 0x00, 0xFF); /* #d00 */
    assert_color_eq(sdl2_color_red_gradient(2), 0xBB, 0x00, 0x00, 0xFF); /* #b00 */
    assert_color_eq(sdl2_color_red_gradient(3), 0x99, 0x00, 0x00, 0xFF); /* #900 */
    assert_color_eq(sdl2_color_red_gradient(4), 0x77, 0x00, 0x00, 0xFF); /* #700 */
    assert_color_eq(sdl2_color_red_gradient(5), 0x55, 0x00, 0x00, 0xFF); /* #500 */
    assert_color_eq(sdl2_color_red_gradient(6), 0x33, 0x00, 0x00, 0xFF); /* #300 */
}

/* TC-05: Red gradient is strictly decreasing in brightness. */
static void test_red_gradient_monotonic(void **state)
{
    (void)state;
    for (int i = 0; i < SDL2C_GRADIENT_STEPS - 1; i++)
    {
        SDL_Color brighter = sdl2_color_red_gradient(i);
        SDL_Color darker = sdl2_color_red_gradient(i + 1);
        assert_true(brighter.r > darker.r);
        assert_int_equal(brighter.g, 0);
        assert_int_equal(brighter.b, 0);
    }
}

/* TC-06: Red gradient out-of-range returns black. */
static void test_red_gradient_out_of_range(void **state)
{
    (void)state;
    assert_color_eq(sdl2_color_red_gradient(-1), 0, 0, 0, 255);
    assert_color_eq(sdl2_color_red_gradient(7), 0, 0, 0, 255);
}

/* =========================================================================
 * Group 3: Green gradient array
 * ========================================================================= */

/* TC-07: Green gradient matches legacy hex values from init.c. */
static void test_green_gradient_values(void **state)
{
    (void)state;
    assert_color_eq(sdl2_color_green_gradient(0), 0x00, 0xFF, 0x00, 0xFF); /* #0f0 */
    assert_color_eq(sdl2_color_green_gradient(1), 0x00, 0xDD, 0x00, 0xFF); /* #0d0 */
    assert_color_eq(sdl2_color_green_gradient(2), 0x00, 0xBB, 0x00, 0xFF); /* #0b0 */
    assert_color_eq(sdl2_color_green_gradient(3), 0x00, 0x99, 0x00, 0xFF); /* #090 */
    assert_color_eq(sdl2_color_green_gradient(4), 0x00, 0x77, 0x00, 0xFF); /* #070 */
    assert_color_eq(sdl2_color_green_gradient(5), 0x00, 0x55, 0x00, 0xFF); /* #050 */
    assert_color_eq(sdl2_color_green_gradient(6), 0x00, 0x33, 0x00, 0xFF); /* #030 */
}

/* TC-08: Green gradient is strictly decreasing in brightness. */
static void test_green_gradient_monotonic(void **state)
{
    (void)state;
    for (int i = 0; i < SDL2C_GRADIENT_STEPS - 1; i++)
    {
        SDL_Color brighter = sdl2_color_green_gradient(i);
        SDL_Color darker = sdl2_color_green_gradient(i + 1);
        assert_int_equal(brighter.r, 0);
        assert_true(brighter.g > darker.g);
        assert_int_equal(brighter.b, 0);
    }
}

/* TC-09: Green gradient out-of-range returns black. */
static void test_green_gradient_out_of_range(void **state)
{
    (void)state;
    assert_color_eq(sdl2_color_green_gradient(-1), 0, 0, 0, 255);
    assert_color_eq(sdl2_color_green_gradient(7), 0, 0, 0, 255);
}

/* =========================================================================
 * Group 4: String name lookup
 * ========================================================================= */

/* TC-10: All 8 named colors resolve by string. */
static void test_by_name_all_named(void **state)
{
    (void)state;
    SDL_Color c;

    assert_true(sdl2_color_by_name("red", &c));
    assert_color_eq(c, 255, 0, 0, 255);

    assert_true(sdl2_color_by_name("tan", &c));
    assert_color_eq(c, 210, 180, 140, 255);

    assert_true(sdl2_color_by_name("yellow", &c));
    assert_color_eq(c, 255, 255, 0, 255);

    assert_true(sdl2_color_by_name("green", &c));
    assert_color_eq(c, 0, 255, 0, 255);

    assert_true(sdl2_color_by_name("white", &c));
    assert_color_eq(c, 255, 255, 255, 255);

    assert_true(sdl2_color_by_name("black", &c));
    assert_color_eq(c, 0, 0, 0, 255);

    assert_true(sdl2_color_by_name("blue", &c));
    assert_color_eq(c, 0, 0, 255, 255);

    assert_true(sdl2_color_by_name("purple", &c));
    assert_color_eq(c, 160, 32, 240, 255);
}

/* TC-11: Name lookup is case-insensitive. */
static void test_by_name_case_insensitive(void **state)
{
    (void)state;
    SDL_Color c;

    assert_true(sdl2_color_by_name("Red", &c));
    assert_color_eq(c, 255, 0, 0, 255);

    assert_true(sdl2_color_by_name("RED", &c));
    assert_color_eq(c, 255, 0, 0, 255);

    assert_true(sdl2_color_by_name("Yellow", &c));
    assert_color_eq(c, 255, 255, 0, 255);
}

/* TC-12: 3-digit hex strings parse correctly. */
static void test_by_name_hex3(void **state)
{
    (void)state;
    SDL_Color c;

    assert_true(sdl2_color_by_name("#f00", &c));
    assert_color_eq(c, 0xFF, 0x00, 0x00, 0xFF);

    assert_true(sdl2_color_by_name("#0f0", &c));
    assert_color_eq(c, 0x00, 0xFF, 0x00, 0xFF);

    assert_true(sdl2_color_by_name("#00f", &c));
    assert_color_eq(c, 0x00, 0x00, 0xFF, 0xFF);

    assert_true(sdl2_color_by_name("#d00", &c));
    assert_color_eq(c, 0xDD, 0x00, 0x00, 0xFF);

    assert_true(sdl2_color_by_name("#fff", &c));
    assert_color_eq(c, 0xFF, 0xFF, 0xFF, 0xFF);

    assert_true(sdl2_color_by_name("#000", &c));
    assert_color_eq(c, 0x00, 0x00, 0x00, 0xFF);
}

/* TC-13: Hex is case-insensitive. */
static void test_by_name_hex_case(void **state)
{
    (void)state;
    SDL_Color c;

    assert_true(sdl2_color_by_name("#F00", &c));
    assert_color_eq(c, 0xFF, 0x00, 0x00, 0xFF);

    assert_true(sdl2_color_by_name("#D00", &c));
    assert_color_eq(c, 0xDD, 0x00, 0x00, 0xFF);
}

/* TC-14: Unknown name returns false. */
static void test_by_name_unknown(void **state)
{
    (void)state;
    SDL_Color c;
    assert_false(sdl2_color_by_name("chartreuse", &c));
    assert_false(sdl2_color_by_name("", &c));
}

/* TC-15: NULL arguments return false. */
static void test_by_name_null(void **state)
{
    (void)state;
    SDL_Color c;
    assert_false(sdl2_color_by_name(NULL, &c));
    assert_false(sdl2_color_by_name("red", NULL));
    assert_false(sdl2_color_by_name(NULL, NULL));
}

/* TC-16: Invalid hex strings return false. */
static void test_by_name_invalid_hex(void **state)
{
    (void)state;
    SDL_Color c;
    assert_false(sdl2_color_by_name("#fg0", &c));  /* invalid hex digit */
    assert_false(sdl2_color_by_name("#ff", &c));   /* too short */
    assert_false(sdl2_color_by_name("#ffff", &c)); /* too long (not 3-digit) */
    assert_false(sdl2_color_by_name("#", &c));     /* hash only */
}

/* =========================================================================
 * Group 5: Color name strings
 * ========================================================================= */

/* TC-17: All color IDs have correct name strings. */
static void test_color_names(void **state)
{
    (void)state;
    assert_string_equal(sdl2_color_name(SDL2C_RED), "red");
    assert_string_equal(sdl2_color_name(SDL2C_TAN), "tan");
    assert_string_equal(sdl2_color_name(SDL2C_YELLOW), "yellow");
    assert_string_equal(sdl2_color_name(SDL2C_GREEN), "green");
    assert_string_equal(sdl2_color_name(SDL2C_WHITE), "white");
    assert_string_equal(sdl2_color_name(SDL2C_BLACK), "black");
    assert_string_equal(sdl2_color_name(SDL2C_BLUE), "blue");
    assert_string_equal(sdl2_color_name(SDL2C_PURPLE), "purple");
}

/* TC-18: Out-of-range ID returns "unknown". */
static void test_color_name_out_of_range(void **state)
{
    (void)state;
    assert_string_equal(sdl2_color_name(SDL2C_COLOR_COUNT), "unknown");
    assert_string_equal(sdl2_color_name((sdl2_color_id_t)-1), "unknown");
}

/* =========================================================================
 * Group 6: Cross-checks with legacy gradient hex values
 * ========================================================================= */

/* TC-19: Gradient index 0 matches the corresponding named color. */
static void test_gradient_brightest_matches_named(void **state)
{
    (void)state;
    SDL_Color red0 = sdl2_color_red_gradient(0);
    SDL_Color red_named = sdl2_color_get(SDL2C_RED);
    assert_int_equal(red0.r, red_named.r);
    assert_int_equal(red0.g, red_named.g);
    assert_int_equal(red0.b, red_named.b);

    SDL_Color green0 = sdl2_color_green_gradient(0);
    SDL_Color green_named = sdl2_color_get(SDL2C_GREEN);
    assert_int_equal(green0.r, green_named.r);
    assert_int_equal(green0.g, green_named.g);
    assert_int_equal(green0.b, green_named.b);
}

/* TC-20: Hex lookup matches gradient array values. */
static void test_hex_lookup_matches_gradient(void **state)
{
    (void)state;
    SDL_Color c;

    /* Spot-check red gradient entries via hex lookup. */
    assert_true(sdl2_color_by_name("#d00", &c));
    SDL_Color r1 = sdl2_color_red_gradient(1);
    assert_int_equal(c.r, r1.r);
    assert_int_equal(c.g, r1.g);
    assert_int_equal(c.b, r1.b);

    assert_true(sdl2_color_by_name("#700", &c));
    SDL_Color r4 = sdl2_color_red_gradient(4);
    assert_int_equal(c.r, r4.r);
    assert_int_equal(c.g, r4.g);
    assert_int_equal(c.b, r4.b);

    /* Spot-check green gradient entries via hex lookup. */
    assert_true(sdl2_color_by_name("#0b0", &c));
    SDL_Color g2 = sdl2_color_green_gradient(2);
    assert_int_equal(c.r, g2.r);
    assert_int_equal(c.g, g2.g);
    assert_int_equal(c.b, g2.b);
}

/* =========================================================================
 * Test runner
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Named color lookup */
        cmocka_unit_test(test_named_colors_x11_values),
        cmocka_unit_test(test_named_colors_opaque),
        cmocka_unit_test(test_get_out_of_range),
        /* Group 2: Red gradient */
        cmocka_unit_test(test_red_gradient_values),
        cmocka_unit_test(test_red_gradient_monotonic),
        cmocka_unit_test(test_red_gradient_out_of_range),
        /* Group 3: Green gradient */
        cmocka_unit_test(test_green_gradient_values),
        cmocka_unit_test(test_green_gradient_monotonic),
        cmocka_unit_test(test_green_gradient_out_of_range),
        /* Group 4: String name lookup */
        cmocka_unit_test(test_by_name_all_named),
        cmocka_unit_test(test_by_name_case_insensitive),
        cmocka_unit_test(test_by_name_hex3),
        cmocka_unit_test(test_by_name_hex_case),
        cmocka_unit_test(test_by_name_unknown),
        cmocka_unit_test(test_by_name_null),
        cmocka_unit_test(test_by_name_invalid_hex),
        /* Group 5: Color name strings */
        cmocka_unit_test(test_color_names),
        cmocka_unit_test(test_color_name_out_of_range),
        /* Group 6: Cross-checks */
        cmocka_unit_test(test_gradient_brightest_matches_named),
        cmocka_unit_test(test_hex_lookup_matches_gradient),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
