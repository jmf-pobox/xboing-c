/*
 * test_sys_priv.c — characterization of the setgid privilege helper.
 *
 * The test process is never setgid, so this pins the unprivileged
 * behavior that submit_score relies on to skip the global board:
 * sys_priv_is_setgid() reports false, and elevate/drop are inert
 * no-ops that succeed (egid already equals rgid).
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "sys_priv.h"

/* Before init, is_setgid must not claim privilege. */
static void test_is_setgid_false_before_init(void **state)
{
    (void)state;
    assert_int_equal(sys_priv_is_setgid(), 0);
}

/* A normal (non-setgid) process: init succeeds, is_setgid stays false,
 * and elevate/drop are no-ops returning success. */
static void test_unprivileged_lifecycle(void **state)
{
    (void)state;
    sys_priv_init();
    assert_int_equal(sys_priv_is_setgid(), 0);
    assert_int_equal(sys_priv_elevate(), 0);
    assert_int_equal(sys_priv_drop(), 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_is_setgid_false_before_init),
        cmocka_unit_test(test_unprivileged_lifecycle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
