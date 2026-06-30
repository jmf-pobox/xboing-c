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

/* Without setgid (the test process is never setgid), the global board is
 * active only when an explicit score-file override is configured. */
static void test_global_board_inactive_without_override(void **state)
{
    (void)state;
    assert_false(sys_priv_global_board_active(NULL));
    assert_false(sys_priv_global_board_active(""));
}

/* A non-empty override (e.g. $XBOING_SCORE_FILE) activates the global
 * board even on an unprivileged process — the tests/admin escape hatch. */
static void test_global_board_active_with_override(void **state)
{
    (void)state;
    assert_true(sys_priv_global_board_active("/tmp/xboing-test-scores.dat"));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_is_setgid_false_before_init),
        cmocka_unit_test(test_unprivileged_lifecycle),
        cmocka_unit_test(test_global_board_inactive_without_override),
        cmocka_unit_test(test_global_board_active_with_override),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
