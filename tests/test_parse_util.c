/*
 * test_parse_util.c — strict integer string parsing.
 *
 * Covers every branch where atoi() would have silently misbehaved:
 * partial parses ("12bogus"), non-numeric input, empty/NULL, overflow,
 * negative-sign handling, range-boundary checks.
 */

#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#include <cmocka.h>

#include "parse_util.h"

static void test_happy_path(void **state)
{
    (void)state;
    int v = -1;
    assert_true(parse_int_in_range("42", 0, 100, &v));
    assert_int_equal(v, 42);
}

static void test_range_boundaries_inclusive(void **state)
{
    (void)state;
    int v = -1;
    assert_true(parse_int_in_range("1", 1, 9, &v));
    assert_int_equal(v, 1);
    assert_true(parse_int_in_range("9", 1, 9, &v));
    assert_int_equal(v, 9);
}

static void test_below_range_rejected(void **state)
{
    (void)state;
    int v = 42;
    assert_false(parse_int_in_range("0", 1, 9, &v));
    assert_int_equal(v, 42); /* out unchanged */
}

static void test_above_range_rejected(void **state)
{
    (void)state;
    int v = 42;
    assert_false(parse_int_in_range("10", 1, 9, &v));
    assert_int_equal(v, 42);
}

/* This is the headline atoi() bug: "12bogus" → 12 silently. */
static void test_partial_parse_rejected(void **state)
{
    (void)state;
    int v = 42;
    assert_false(parse_int_in_range("12bogus", 1, 100, &v));
    assert_int_equal(v, 42);
}

static void test_trailing_whitespace_rejected(void **state)
{
    (void)state;
    int v = 42;
    assert_false(parse_int_in_range("12 ", 1, 100, &v));
    assert_int_equal(v, 42);
}

static void test_non_numeric_rejected(void **state)
{
    (void)state;
    int v = 42;
    assert_false(parse_int_in_range("abc", 1, 100, &v));
    assert_int_equal(v, 42);
}

static void test_empty_rejected(void **state)
{
    (void)state;
    int v = 42;
    assert_false(parse_int_in_range("", 1, 100, &v));
    assert_int_equal(v, 42);
}

static void test_null_safe(void **state)
{
    (void)state;
    int v = 42;
    assert_false(parse_int_in_range(NULL, 1, 100, &v));
    assert_int_equal(v, 42);
    assert_false(parse_int_in_range("5", 1, 100, NULL));
}

static void test_negative_in_signed_range(void **state)
{
    (void)state;
    int v = 0;
    assert_true(parse_int_in_range("-5", -10, 10, &v));
    assert_int_equal(v, -5);
}

static void test_negative_outside_positive_range(void **state)
{
    (void)state;
    int v = 42;
    assert_false(parse_int_in_range("-1", 1, 9, &v));
    assert_int_equal(v, 42);
}

static void test_overflow_rejected(void **state)
{
    (void)state;
    int v = 42;
    /* Well past LONG_MAX on every supported platform. */
    assert_false(parse_int_in_range("99999999999999999999", 1, 100, &v));
    assert_int_equal(v, 42);
}

static void test_int_max_accepted_at_upper_bound(void **state)
{
    (void)state;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", INT_MAX);
    int v = 0;
    assert_true(parse_int_in_range(buf, 1, INT_MAX, &v));
    assert_int_equal(v, INT_MAX);
}

static void test_leading_plus_accepted(void **state)
{
    (void)state;
    int v = 0;
    assert_true(parse_int_in_range("+5", 1, 9, &v));
    assert_int_equal(v, 5);
}

static void test_hex_prefix_rejected(void **state)
{
    (void)state;
    /* Base-10 parsing: "0x" is not numeric, "0xA" fails the *end != '\0' check. */
    int v = 42;
    assert_false(parse_int_in_range("0xA", 1, 100, &v));
    assert_int_equal(v, 42);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_happy_path),
        cmocka_unit_test(test_range_boundaries_inclusive),
        cmocka_unit_test(test_below_range_rejected),
        cmocka_unit_test(test_above_range_rejected),
        cmocka_unit_test(test_partial_parse_rejected),
        cmocka_unit_test(test_trailing_whitespace_rejected),
        cmocka_unit_test(test_non_numeric_rejected),
        cmocka_unit_test(test_empty_rejected),
        cmocka_unit_test(test_null_safe),
        cmocka_unit_test(test_negative_in_signed_range),
        cmocka_unit_test(test_negative_outside_positive_range),
        cmocka_unit_test(test_overflow_rejected),
        cmocka_unit_test(test_int_max_accepted_at_upper_bound),
        cmocka_unit_test(test_leading_plus_accepted),
        cmocka_unit_test(test_hex_prefix_rejected),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
