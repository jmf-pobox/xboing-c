/*
 * test_skeleton.c — CMocka harness smoke test.
 *
 * Proves the test infrastructure works: CMocka links, ctest discovers
 * and runs the test, assertions pass. Replace with real tests as
 * characterization testing begins.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

static void test_true_is_true(void **state)
{
    (void)state;
    assert_true(1);
}

static void test_null_is_null(void **state)
{
    (void)state;
    assert_null(NULL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_true_is_true),
        cmocka_unit_test(test_null_is_null),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
