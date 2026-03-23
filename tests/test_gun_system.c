/*
 * test_gun_system.c — CMocka tests for gun_system module.
 *
 * Characterization tests for the pure C gun/bullet system port.
 * All tests are deterministic — no randomness, no I/O.
 * Callbacks are stubbed to track invocations and return configured values.
 *
 * Test groups:
 *   1. Lifecycle (3 tests)
 *   2. Ammo management (6 tests)
 *   3. Shooting — normal mode (4 tests)
 *   4. Shooting — fast gun mode (4 tests)
 *   5. Bullet movement and top-wall tinks (3 tests)
 *   6. Collision — ball hit (2 tests)
 *   7. Collision — eyedude hit (2 tests)
 *   8. Collision — block hit (2 tests)
 *   9. Collision priority (1 test)
 *  10. Tink expiry (2 tests)
 *  11. Clear (2 tests)
 *  12. Render queries (3 tests)
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

/* CMocka must come after setjmp.h */
#include <cmocka.h>

#include "gun_system.h"

/* Production values matching legacy constants */
#define PLAY_HEIGHT 580

/* =========================================================================
 * Stub callback state
 * ========================================================================= */

typedef struct
{
    /* Sound tracking */
    int sound_count;
    char last_sound[64];

    /* Block hit */
    int block_hit_return;  /* Nonzero = collision detected */
    int block_hit_out_row; /* Row to return via out param */
    int block_hit_out_col; /* Col to return via out param */
    int block_hit_count;   /* Number of on_block_hit calls */
    int block_hit_last_row;
    int block_hit_last_col;

    /* Ball hit */
    int ball_hit_return;      /* Ball index to return (-1 = miss) */
    int ball_hit_max;         /* Max hits before returning -1 (0 = unlimited) */
    int ball_hit_check_count; /* Number of check_ball_hit calls */
    int ball_hit_count;       /* Number of on_ball_hit calls */
    int ball_hit_last_index;

    /* Eyedude hit */
    int eyedude_hit_return; /* Nonzero = hit */
    int eyedude_hit_count;  /* Number of on_eyedude_hit calls */

    /* Ball waiting */
    int ball_waiting_return;
} stub_state_t;

static void reset_stub_state(stub_state_t *s)
{
    memset(s, 0, sizeof(*s));
    s->ball_hit_return = -1; /* Default: no ball hit */
}

/* =========================================================================
 * Stub callbacks
 * ========================================================================= */

static int stub_check_block_hit(int bx, int by, int *out_row, int *out_col, void *ud)
{
    (void)bx;
    (void)by;
    const stub_state_t *s = ud;
    if (s->block_hit_return)
    {
        *out_row = s->block_hit_out_row;
        *out_col = s->block_hit_out_col;
    }
    return s->block_hit_return;
}

static void stub_on_block_hit(int row, int col, void *ud)
{
    stub_state_t *s = ud;
    s->block_hit_count++;
    s->block_hit_last_row = row;
    s->block_hit_last_col = col;
}

static int stub_check_ball_hit(int bx, int by, void *ud)
{
    (void)bx;
    (void)by;
    stub_state_t *s = ud;
    s->ball_hit_check_count++;
    /* If max is set, only return hit for the first N checks */
    if (s->ball_hit_max > 0 && s->ball_hit_check_count > s->ball_hit_max)
    {
        return -1;
    }
    return s->ball_hit_return;
}

static void stub_on_ball_hit(int ball_index, void *ud)
{
    stub_state_t *s = ud;
    s->ball_hit_count++;
    s->ball_hit_last_index = ball_index;
}

static int stub_check_eyedude_hit(int bx, int by, void *ud)
{
    (void)bx;
    (void)by;
    const stub_state_t *s = ud;
    return s->eyedude_hit_return;
}

static void stub_on_eyedude_hit(void *ud)
{
    stub_state_t *s = ud;
    s->eyedude_hit_count++;
}

static void stub_on_sound(const char *name, void *ud)
{
    stub_state_t *s = ud;
    s->sound_count++;
    strncpy(s->last_sound, name, sizeof(s->last_sound) - 1);
    s->last_sound[sizeof(s->last_sound) - 1] = '\0';
}

static int stub_is_ball_waiting(void *ud)
{
    const stub_state_t *s = ud;
    return s->ball_waiting_return;
}

/* =========================================================================
 * Helper: create a gun system with all stubs wired
 * ========================================================================= */

static gun_system_t *create_test_ctx(stub_state_t *s)
{
    reset_stub_state(s);

    gun_system_callbacks_t cb;
    memset(&cb, 0, sizeof(cb));
    cb.check_block_hit = stub_check_block_hit;
    cb.on_block_hit = stub_on_block_hit;
    cb.check_ball_hit = stub_check_ball_hit;
    cb.on_ball_hit = stub_on_ball_hit;
    cb.check_eyedude_hit = stub_check_eyedude_hit;
    cb.on_eyedude_hit = stub_on_eyedude_hit;
    cb.on_sound = stub_on_sound;
    cb.is_ball_waiting = stub_is_ball_waiting;

    gun_system_status_t status;
    gun_system_t *ctx = gun_system_create(PLAY_HEIGHT, &cb, s, &status);
    assert_non_null(ctx);
    assert_int_equal(status, GUN_SYS_OK);
    return ctx;
}

static gun_system_env_t make_env(int frame, int paddle_pos, int paddle_size, int fast_gun)
{
    gun_system_env_t env;
    env.frame = frame;
    env.paddle_pos = paddle_pos;
    env.paddle_size = paddle_size;
    env.fast_gun = fast_gun;
    return env;
}

/* =========================================================================
 * Group 1: Lifecycle
 * ========================================================================= */

static void test_create_destroy(void **state)
{
    (void)state;
    gun_system_status_t st;
    gun_system_t *ctx = gun_system_create(PLAY_HEIGHT, NULL, NULL, &st);
    assert_non_null(ctx);
    assert_int_equal(st, GUN_SYS_OK);
    gun_system_destroy(ctx);
}

static void test_create_initial_state(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);

    /* Initial ammo: 0 */
    assert_int_equal(gun_system_get_ammo(ctx), 0);

    /* Unlimited off */
    assert_int_equal(gun_system_get_unlimited(ctx), 0);

    /* No active bullets or tinks */
    assert_int_equal(gun_system_get_active_bullet_count(ctx), 0);
    assert_int_equal(gun_system_get_active_tink_count(ctx), 0);

    gun_system_destroy(ctx);
}

static void test_destroy_null_safe(void **state)
{
    (void)state;
    gun_system_destroy(NULL); /* Should not crash */
}

/* =========================================================================
 * Group 2: Ammo management
 * ========================================================================= */

static void test_set_ammo(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);

    gun_system_set_ammo(ctx, 10);
    assert_int_equal(gun_system_get_ammo(ctx), 10);

    gun_system_set_ammo(ctx, 0);
    assert_int_equal(gun_system_get_ammo(ctx), 0);

    gun_system_destroy(ctx);
}

static void test_add_ammo(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);

    gun_system_set_ammo(ctx, 5);
    gun_system_add_ammo(ctx);
    assert_int_equal(gun_system_get_ammo(ctx), 6);

    gun_system_destroy(ctx);
}

static void test_add_ammo_clamps_at_max(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);

    gun_system_set_ammo(ctx, GUN_MAX_AMMO);
    gun_system_add_ammo(ctx);
    assert_int_equal(gun_system_get_ammo(ctx), GUN_MAX_AMMO);

    gun_system_destroy(ctx);
}

static void test_use_ammo(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);

    gun_system_set_ammo(ctx, 5);
    int remaining = gun_system_use_ammo(ctx);
    assert_int_equal(remaining, 4);
    assert_int_equal(gun_system_get_ammo(ctx), 4);

    gun_system_destroy(ctx);
}

static void test_use_ammo_clamps_at_zero(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);

    gun_system_set_ammo(ctx, 0);
    int remaining = gun_system_use_ammo(ctx);
    assert_int_equal(remaining, 0);

    gun_system_destroy(ctx);
}

static void test_unlimited_ammo_no_decrement(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);

    gun_system_set_ammo(ctx, 5);
    gun_system_set_unlimited(ctx, 1);
    assert_int_equal(gun_system_get_unlimited(ctx), 1);

    int remaining = gun_system_use_ammo(ctx);
    assert_int_equal(remaining, 5); /* Not decremented */
    assert_int_equal(gun_system_get_ammo(ctx), 5);

    gun_system_destroy(ctx);
}

/* =========================================================================
 * Group 3: Shooting — normal mode
 * ========================================================================= */

static void test_shoot_spawns_bullet(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 4);
    int fired = gun_system_shoot(ctx, &env);

    assert_int_equal(fired, 1);
    assert_int_equal(gun_system_get_active_bullet_count(ctx), 1);
    assert_int_equal(gun_system_get_ammo(ctx), 3);
    assert_string_equal(s.last_sound, "shotgun");

    gun_system_destroy(ctx);
}

static void test_shoot_no_ammo_plays_click(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    /* 0 ammo */
    int fired = gun_system_shoot(ctx, &env);
    assert_int_equal(fired, 0);
    assert_int_equal(gun_system_get_active_bullet_count(ctx), 0);
    assert_string_equal(s.last_sound, "click");

    gun_system_destroy(ctx);
}

static void test_shoot_ball_waiting_blocked(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 4);
    s.ball_waiting_return = 1;

    int fired = gun_system_shoot(ctx, &env);
    assert_int_equal(fired, 0);
    assert_int_equal(gun_system_get_active_bullet_count(ctx), 0);
    assert_int_equal(gun_system_get_ammo(ctx), 4); /* Not consumed */

    gun_system_destroy(ctx);
}

static void test_shoot_normal_one_bullet_at_a_time(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 10);

    /* First shot: succeeds */
    int fired1 = gun_system_shoot(ctx, &env);
    assert_int_equal(fired1, 1);
    assert_int_equal(gun_system_get_active_bullet_count(ctx), 1);

    /* Second shot: fails (slot 0 occupied, normal mode stops at first) */
    int fired2 = gun_system_shoot(ctx, &env);
    assert_int_equal(fired2, 0);
    assert_int_equal(gun_system_get_active_bullet_count(ctx), 1);

    gun_system_destroy(ctx);
}

/* =========================================================================
 * Group 4: Shooting — fast gun mode
 * ========================================================================= */

static void test_fast_gun_spawns_two_bullets(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 60, 1); /* fast_gun = 1, size = 60 */

    gun_system_set_ammo(ctx, 4);
    int fired = gun_system_shoot(ctx, &env);

    assert_int_equal(fired, 1);
    assert_int_equal(gun_system_get_active_bullet_count(ctx), 2);
    assert_int_equal(gun_system_get_ammo(ctx), 3); /* Only 1 ammo consumed */

    /* Verify bullet positions: paddle_pos ± (size/3) = 200 ± 20 */
    gun_system_bullet_info_t info;
    gun_system_get_bullet_info(ctx, 0, &info);
    assert_int_equal(info.active, 1);
    assert_int_equal(info.x, 180); /* 200 - 20 */

    gun_system_get_bullet_info(ctx, 1, &info);
    assert_int_equal(info.active, 1);
    assert_int_equal(info.x, 220); /* 200 + 20 */

    gun_system_destroy(ctx);
}

static void test_fast_gun_multiple_shots(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 60, 1);

    gun_system_set_ammo(ctx, 10);

    gun_system_shoot(ctx, &env);
    gun_system_shoot(ctx, &env);
    gun_system_shoot(ctx, &env);

    assert_int_equal(gun_system_get_active_bullet_count(ctx), 6); /* 3 shots * 2 */
    assert_int_equal(gun_system_get_ammo(ctx), 7);

    gun_system_destroy(ctx);
}

static void test_fast_gun_full_array(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 60, 1);

    gun_system_set_ammo(ctx, 100);
    gun_system_set_unlimited(ctx, 1);

    /* Fill all 40 slots (20 shots * 2 bullets) */
    for (int i = 0; i < 20; i++)
    {
        gun_system_shoot(ctx, &env);
    }
    assert_int_equal(gun_system_get_active_bullet_count(ctx), GUN_MAX_BULLETS);

    /* Next shot should fail (array full) */
    int fired = gun_system_shoot(ctx, &env);
    assert_int_equal(fired, 0);

    gun_system_destroy(ctx);
}

static void test_fast_gun_one_slot_free(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 60, 1);

    gun_system_set_ammo(ctx, 100);
    gun_system_set_unlimited(ctx, 1);

    /* Fill all 40 slots (20 fast-gun shots * 2 bullets each) */
    for (int i = 0; i < 20; i++)
    {
        gun_system_shoot(ctx, &env);
    }
    assert_int_equal(gun_system_get_active_bullet_count(ctx), 40);

    /* Kill exactly one bullet using one-shot ball_hit callback:
     * ball_hit_max=1 means only the first check returns a hit. */
    s.ball_hit_return = 0;
    s.ball_hit_max = 1;
    env.frame = 3; /* 3 % 3 == 0, triggers bullet update */
    gun_system_update(ctx, &env);
    assert_int_equal(gun_system_get_active_bullet_count(ctx), 39);

    /* Reset stubs, try fast-gun shot with only 1 free slot */
    s.ball_hit_return = -1;
    // cppcheck-suppress redundantAssignment
    s.ball_hit_max = 0;
    s.ball_hit_check_count = 0;
    s.sound_count = 0;
    memset(s.last_sound, 0, sizeof(s.last_sound));

    int fired = gun_system_shoot(ctx, &env);

    /* First bullet spawns (success), second fails (array full).
     * With the fix, status = s1 || s2 = true, so ammo consumed + sound. */
    assert_int_equal(fired, 1);
    assert_int_equal(gun_system_get_active_bullet_count(ctx), 40);
    assert_string_equal(s.last_sound, "shotgun");

    gun_system_destroy(ctx);
}

/* =========================================================================
 * Group 5: Bullet movement and top-wall tinks
 * ========================================================================= */

static void test_bullet_moves_upward(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    /* frame=0 triggers update (0 % 3 == 0) */
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 4);
    gun_system_shoot(ctx, &env);

    /* Bullet starts at BULLET_START_Y = 580 - 40 = 540, x = 200 */
    gun_system_bullet_info_t info;
    gun_system_get_bullet_info(ctx, 0, &info);
    assert_int_equal(info.active, 1);
    assert_int_equal(info.y, 540);
    assert_int_equal(info.from_y, 540);         /* from == pos at spawn */
    assert_int_equal(info.ticks_since_move, 0); /* just spawned */

    /* Non-movement frames: ticks_since_move increments */
    env.frame = 1;
    gun_system_update(ctx, &env);
    gun_system_get_bullet_info(ctx, 0, &info);
    assert_int_equal(info.y, 540); /* hasn't moved yet */
    assert_int_equal(info.ticks_since_move, 1);

    env.frame = 2;
    gun_system_update(ctx, &env);
    gun_system_get_bullet_info(ctx, 0, &info);
    assert_int_equal(info.y, 540);
    assert_int_equal(info.ticks_since_move, 2);

    /* Update on frame 3 (3 % 3 == 0): ypos = 540 + (-7) = 533 */
    env.frame = 3;
    gun_system_update(ctx, &env);

    gun_system_get_bullet_info(ctx, 0, &info);
    assert_int_equal(info.active, 1);
    assert_int_equal(info.y, 533);
    assert_int_equal(info.from_y, 540);         /* from == position before move */
    assert_int_equal(info.ticks_since_move, 0); /* just moved */

    gun_system_destroy(ctx);
}

static void test_bullet_skips_non_update_frames(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 4);
    gun_system_shoot(ctx, &env);

    /* Update on frame 1 (1 % 3 != 0): bullet should NOT move */
    env.frame = 1;
    gun_system_update(ctx, &env);

    gun_system_bullet_info_t info;
    gun_system_get_bullet_info(ctx, 0, &info);
    assert_int_equal(info.y, 540); /* Unchanged */

    gun_system_destroy(ctx);
}

static void test_bullet_creates_tink_at_top(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 4);
    gun_system_shoot(ctx, &env);

    /* Move bullet upward until it goes past top.
     * bullet_start_y = 540, dy = -7
     * After N updates: y = 540 + N*(-7) = 540 - 7N
     * Bullet disappears when y < -GUN_BULLET_HC = -8
     * 540 - 7N < -8 => N > 78.28... => N = 79 updates
     * Each update at frame % 3 == 0, so frames = 79 * 3 = 237
     */
    for (int f = 3; f <= 237; f += 3)
    {
        env.frame = f;
        gun_system_update(ctx, &env);
    }

    assert_int_equal(gun_system_get_active_bullet_count(ctx), 0);
    assert_int_equal(gun_system_get_active_tink_count(ctx), 1);

    /* Tink should be at the bullet's x position */
    gun_system_tink_info_t tinfo;
    gun_system_get_tink_info(ctx, 0, &tinfo);
    assert_int_equal(tinfo.active, 1);
    assert_int_equal(tinfo.x, 200);

    /* "shoot" sound played when tink created */
    assert_string_equal(s.last_sound, "shoot");

    gun_system_destroy(ctx);
}

/* =========================================================================
 * Group 6: Collision — ball hit
 * ========================================================================= */

static void test_bullet_kills_ball(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 4);
    gun_system_shoot(ctx, &env);
    s.sound_count = 0;

    /* Configure ball hit on next update */
    s.ball_hit_return = 2; /* Ball index 2 */

    env.frame = 3;
    gun_system_update(ctx, &env);

    /* Bullet consumed */
    assert_int_equal(gun_system_get_active_bullet_count(ctx), 0);

    /* Ball hit callback fired */
    assert_int_equal(s.ball_hit_count, 1);
    assert_int_equal(s.ball_hit_last_index, 2);

    /* "ballshot" sound played */
    assert_string_equal(s.last_sound, "ballshot");

    gun_system_destroy(ctx);
}

static void test_ball_hit_prevents_block_check(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 4);
    gun_system_shoot(ctx, &env);

    /* Both ball and block would hit */
    s.ball_hit_return = 0;
    s.block_hit_return = 1;
    s.block_hit_out_row = 5;
    s.block_hit_out_col = 3;

    env.frame = 3;
    gun_system_update(ctx, &env);

    /* Ball hit takes priority — block hit NOT fired */
    assert_int_equal(s.ball_hit_count, 1);
    assert_int_equal(s.block_hit_count, 0);

    gun_system_destroy(ctx);
}

/* =========================================================================
 * Group 7: Collision — eyedude hit
 * ========================================================================= */

static void test_bullet_kills_eyedude(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 4);
    gun_system_shoot(ctx, &env);

    /* Configure eyedude hit */
    s.eyedude_hit_return = 1;

    env.frame = 3;
    gun_system_update(ctx, &env);

    /* Bullet consumed */
    assert_int_equal(gun_system_get_active_bullet_count(ctx), 0);

    /* Eyedude hit callback fired */
    assert_int_equal(s.eyedude_hit_count, 1);

    gun_system_destroy(ctx);
}

static void test_eyedude_hit_prevents_block_check(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 4);
    gun_system_shoot(ctx, &env);

    /* Both eyedude and block would hit, but no ball */
    s.ball_hit_return = -1;
    s.eyedude_hit_return = 1;
    s.block_hit_return = 1;
    s.block_hit_out_row = 5;
    s.block_hit_out_col = 3;

    env.frame = 3;
    gun_system_update(ctx, &env);

    /* Eyedude hit takes priority over block */
    assert_int_equal(s.eyedude_hit_count, 1);
    assert_int_equal(s.block_hit_count, 0);

    gun_system_destroy(ctx);
}

/* =========================================================================
 * Group 8: Collision — block hit
 * ========================================================================= */

static void test_bullet_hits_block(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 4);
    gun_system_shoot(ctx, &env);

    /* Configure block hit */
    s.block_hit_return = 1;
    s.block_hit_out_row = 10;
    s.block_hit_out_col = 4;

    env.frame = 3;
    gun_system_update(ctx, &env);

    /* Bullet consumed */
    assert_int_equal(gun_system_get_active_bullet_count(ctx), 0);

    /* Block hit callback fired with correct row/col */
    assert_int_equal(s.block_hit_count, 1);
    assert_int_equal(s.block_hit_last_row, 10);
    assert_int_equal(s.block_hit_last_col, 4);

    gun_system_destroy(ctx);
}

static void test_no_collision_bullet_continues(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 4);
    gun_system_shoot(ctx, &env);

    /* No hits configured (defaults) */

    env.frame = 3;
    gun_system_update(ctx, &env);

    /* Bullet still active and moved */
    assert_int_equal(gun_system_get_active_bullet_count(ctx), 1);

    gun_system_bullet_info_t info;
    gun_system_get_bullet_info(ctx, 0, &info);
    assert_int_equal(info.active, 1);
    assert_int_equal(info.y, 533); /* 540 + (-7) */

    gun_system_destroy(ctx);
}

/* =========================================================================
 * Group 9: Collision priority
 * ========================================================================= */

static void test_collision_priority_ball_over_eyedude_over_block(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 4);
    gun_system_shoot(ctx, &env);

    /* All three would hit */
    s.ball_hit_return = 0;
    s.eyedude_hit_return = 1;
    s.block_hit_return = 1;
    s.block_hit_out_row = 5;
    s.block_hit_out_col = 3;

    env.frame = 3;
    gun_system_update(ctx, &env);

    /* Ball hit wins */
    assert_int_equal(s.ball_hit_count, 1);
    assert_int_equal(s.eyedude_hit_count, 0);
    assert_int_equal(s.block_hit_count, 0);

    gun_system_destroy(ctx);
}

/* =========================================================================
 * Group 10: Tink expiry
 * ========================================================================= */

static void test_tink_expires_after_delay(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 4);
    gun_system_shoot(ctx, &env);

    /* Move bullet to top to create tink */
    for (int f = 3; f <= 237; f += 3)
    {
        env.frame = f;
        gun_system_update(ctx, &env);
    }
    assert_int_equal(gun_system_get_active_tink_count(ctx), 1);

    /* Tink was created at frame 237, expires at 237 + TINK_DELAY = 337 */
    /* Frame 336: tink still active */
    env.frame = 336;
    gun_system_update(ctx, &env);
    assert_int_equal(gun_system_get_active_tink_count(ctx), 1);

    /* Frame 337: tink expires */
    env.frame = 337;
    gun_system_update(ctx, &env);
    assert_int_equal(gun_system_get_active_tink_count(ctx), 0);

    gun_system_destroy(ctx);
}

static void test_tinks_checked_every_frame(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 0);

    gun_system_set_ammo(ctx, 4);
    gun_system_shoot(ctx, &env);

    /* Move bullet to top */
    for (int f = 3; f <= 237; f += 3)
    {
        env.frame = f;
        gun_system_update(ctx, &env);
    }
    assert_int_equal(gun_system_get_active_tink_count(ctx), 1);

    /* Tink expires at 337. Update on a non-bullet-update frame (338 % 3 != 0) */
    env.frame = 338;
    gun_system_update(ctx, &env);
    assert_int_equal(gun_system_get_active_tink_count(ctx), 0);

    gun_system_destroy(ctx);
}

/* =========================================================================
 * Group 11: Clear
 * ========================================================================= */

static void test_clear_removes_bullets_and_tinks(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);
    gun_system_env_t env = make_env(0, 200, 50, 1); /* fast gun */

    gun_system_set_ammo(ctx, 10);
    gun_system_shoot(ctx, &env);
    gun_system_shoot(ctx, &env);
    assert_true(gun_system_get_active_bullet_count(ctx) > 0);

    gun_system_clear(ctx);

    assert_int_equal(gun_system_get_active_bullet_count(ctx), 0);
    assert_int_equal(gun_system_get_active_tink_count(ctx), 0);

    gun_system_destroy(ctx);
}

static void test_clear_preserves_ammo(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);

    gun_system_set_ammo(ctx, 15);
    gun_system_clear(ctx);

    assert_int_equal(gun_system_get_ammo(ctx), 15);

    gun_system_destroy(ctx);
}

/* =========================================================================
 * Group 12: Render queries
 * ========================================================================= */

static void test_bullet_info_inactive_slot(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);

    gun_system_bullet_info_t info;
    gun_system_status_t st = gun_system_get_bullet_info(ctx, 0, &info);

    assert_int_equal(st, GUN_SYS_OK);
    assert_int_equal(info.active, 0);

    gun_system_destroy(ctx);
}

static void test_bullet_info_out_of_bounds(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);

    gun_system_bullet_info_t info;
    gun_system_status_t st = gun_system_get_bullet_info(ctx, GUN_MAX_BULLETS, &info);
    assert_int_equal(st, GUN_SYS_ERR_OUT_OF_BOUNDS);

    st = gun_system_get_bullet_info(ctx, -1, &info);
    assert_int_equal(st, GUN_SYS_ERR_OUT_OF_BOUNDS);

    gun_system_destroy(ctx);
}

static void test_tink_info_out_of_bounds(void **state)
{
    (void)state;
    stub_state_t s;
    gun_system_t *ctx = create_test_ctx(&s);

    gun_system_tink_info_t info;
    gun_system_status_t st = gun_system_get_tink_info(ctx, GUN_MAX_TINKS, &info);
    assert_int_equal(st, GUN_SYS_ERR_OUT_OF_BOUNDS);

    gun_system_destroy(ctx);
}

/* =========================================================================
 * Main — register all test groups
 * ========================================================================= */

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Group 1: Lifecycle */
        cmocka_unit_test(test_create_destroy),
        cmocka_unit_test(test_create_initial_state),
        cmocka_unit_test(test_destroy_null_safe),

        /* Group 2: Ammo management */
        cmocka_unit_test(test_set_ammo),
        cmocka_unit_test(test_add_ammo),
        cmocka_unit_test(test_add_ammo_clamps_at_max),
        cmocka_unit_test(test_use_ammo),
        cmocka_unit_test(test_use_ammo_clamps_at_zero),
        cmocka_unit_test(test_unlimited_ammo_no_decrement),

        /* Group 3: Shooting — normal mode */
        cmocka_unit_test(test_shoot_spawns_bullet),
        cmocka_unit_test(test_shoot_no_ammo_plays_click),
        cmocka_unit_test(test_shoot_ball_waiting_blocked),
        cmocka_unit_test(test_shoot_normal_one_bullet_at_a_time),

        /* Group 4: Shooting — fast gun mode */
        cmocka_unit_test(test_fast_gun_spawns_two_bullets),
        cmocka_unit_test(test_fast_gun_multiple_shots),
        cmocka_unit_test(test_fast_gun_full_array),
        cmocka_unit_test(test_fast_gun_one_slot_free),

        /* Group 5: Bullet movement and top-wall tinks */
        cmocka_unit_test(test_bullet_moves_upward),
        cmocka_unit_test(test_bullet_skips_non_update_frames),
        cmocka_unit_test(test_bullet_creates_tink_at_top),

        /* Group 6: Collision — ball hit */
        cmocka_unit_test(test_bullet_kills_ball),
        cmocka_unit_test(test_ball_hit_prevents_block_check),

        /* Group 7: Collision — eyedude hit */
        cmocka_unit_test(test_bullet_kills_eyedude),
        cmocka_unit_test(test_eyedude_hit_prevents_block_check),

        /* Group 8: Collision — block hit */
        cmocka_unit_test(test_bullet_hits_block),
        cmocka_unit_test(test_no_collision_bullet_continues),

        /* Group 9: Collision priority */
        cmocka_unit_test(test_collision_priority_ball_over_eyedude_over_block),

        /* Group 10: Tink expiry */
        cmocka_unit_test(test_tink_expires_after_delay),
        cmocka_unit_test(test_tinks_checked_every_frame),

        /* Group 11: Clear */
        cmocka_unit_test(test_clear_removes_bullets_and_tinks),
        cmocka_unit_test(test_clear_preserves_ammo),

        /* Group 12: Render queries */
        cmocka_unit_test(test_bullet_info_inactive_slot),
        cmocka_unit_test(test_bullet_info_out_of_bounds),
        cmocka_unit_test(test_tink_info_out_of_bounds),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
