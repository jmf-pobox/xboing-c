/*
 * game_modes.c -- Mode handlers for the SDL2 game state machine.
 *
 * Each game mode (presents, intro, game, pause, bonus, highscore, etc.)
 * has on_enter/on_update/on_exit handlers registered with sdl2_state.
 *
 * The on_update handler is called every frame by sdl2_state_update().
 * It drives the module's update function and checks for transitions.
 */

#include "game_modes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pwd.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "ball_system.h"
#include "block_system.h"
#include "bonus_system.h"
#include "demo_system.h"
#include "dialogue_system.h"
#include "editor_system.h"
#include "eyedude_system.h"
#include "game_callbacks.h"
#include "game_context.h"
#include "game_input.h"
#include "game_render.h"
#include "game_rules.h"
#include "gun_system.h"
#include "highscore_io.h"
#include "highscore_system.h"
#include "intro_system.h"
#include "keys_system.h"
#include "level_system.h"
#include "message_system.h"
#include "paddle_system.h"
#include "paths.h"
#include "presents_system.h"
#include "score_system.h"
#include "sdl2_audio.h"
#include "sdl2_cursor.h"
#include "sdl2_input.h"
#include "sdl2_loop.h"
#include "sdl2_state.h"
#include "sfx_system.h"
#include "special_system.h"
#include "sys_priv.h"

/* =========================================================================
 * MODE_GAME — core gameplay
 * ========================================================================= */

/* Play area position in window (from legacy stage.c) */
#define PLAY_AREA_X 35
#define PLAY_AREA_Y 60

/* Dialogue result pending flags — set before push_dialogue, consumed
 * in mode exit/enter handlers.  See game_modes.h for API. */
static int wisdom_pending;
static int quit_pending;
static int abort_pending;
static int level_pending;

/* Stashed when the wisdom dialogue is pushed; consumed by the post-wisdom
 * insert path so personal+global rows agree on score/time/name. */
static unsigned long pending_final_score;
static unsigned long pending_game_time;
static unsigned long pending_ts;
static char pending_name[HIGHSCORE_NAME_LEN];

void game_modes_set_quit_pending(void)
{
    quit_pending = 1;
}
void game_modes_set_abort_pending(void)
{
    abort_pending = 1;
}
void game_modes_set_level_pending(void)
{
    level_pending = 1;
}

static void start_new_game(game_ctx_t *ctx)
{
    /* Reset game state */
    ctx->level_number = ctx->start_level;
    ctx->attract_level_display = 0;
    ctx->lives_left = 3;
    ctx->game_active = true;
    ctx->score_submitted = false;
    wisdom_pending = 0;
    quit_pending = 0;
    abort_pending = 0;
    level_pending = 0;
    ctx->game_start = time(NULL);
    ctx->paused_seconds = 0;
    ctx->bonus_block_active = false;
    ctx->next_bonus_frame = 0;
    ctx->user_tilts = 0;
    ctx->bonus_count = 0;

    /* Reset modules */
    score_system_set(ctx->score, 0);
    special_system_turn_off(ctx->special);
    gun_system_set_ammo(ctx->gun, GUN_AMMO_PER_LEVEL);
    paddle_system_reset(ctx->paddle);
    paddle_system_set_size(ctx->paddle, PADDLE_SIZE_HUGE);
    ball_system_clear_all(ctx->ball);

    /* Clear level state before loading (prevents stale blocks if load fails) */
    block_system_clear_all(ctx->block);
    gun_system_clear(ctx->gun);

    /* Load the starting level */
    int file_num = level_system_wrap_number(ctx->level_number);
    char filename[32];
    snprintf(filename, sizeof(filename), "level%02d.data", file_num);

    char level_path[PATHS_MAX_PATH];
    if (paths_level_file(&ctx->paths, filename, level_path, sizeof(level_path)) == PATHS_OK)
    {
        level_system_advance_background(ctx->level);
        level_system_load_file(ctx->level, level_path);
    }
    else
    {
        fprintf(stderr, "Warning: could not find level file: %s\n", filename);
    }

    /* Initialize timer from level file time bonus */
    ctx->time_bonus_total = level_system_get_time_bonus(ctx->level);
    ctx->time_remaining = ctx->time_bonus_total;
    ctx->timer_frame_acc = 0;

    /* Display level title in the message bar and register it as the
     * default for auto-clear messages to revert to.  Mirrors
     * original/file.c:SetupStage:148-150, which calls
     * SetCurrentMessage("- LevelName -", True) unconditionally on
     * every new game and level load. */
    const char *title = level_system_get_title(ctx->level);
    char msg[80];
    if (title && title[0] != '\0')
        snprintf(msg, sizeof(msg), "- %s -", title);
    else
        snprintf(msg, sizeof(msg), "- Untitled -");
    message_system_set_default(ctx->message, msg);
    {
        int frame = (int)sdl2_state_frame(ctx->state);
        message_system_set(ctx->message, msg, 0, frame);
    }

    /* Place ball on paddle */
    ball_system_env_t env = game_callbacks_ball_env(ctx);
    ball_system_reset_start(ctx->ball, &env);

    if (ctx->audio)
        sdl2_audio_play(ctx->audio, "buzzer");
}

static void mode_game_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    /* Abort dialogue result — checked first because sdl2_state_previous()
     * returns SDL2ST_DIALOGUE after pop, not the pre-dialogue mode.
     * The flag carries the intent.  See peer review finding #1. */
    if (abort_pending)
    {
        abort_pending = 0;
        if (!dialogue_system_was_cancelled(ctx->dialogue))
        {
            const char *ans = dialogue_system_get_input(ctx->dialogue);
            if (ans && (ans[0] == 'y' || ans[0] == 'Y'))
            {
                /* Don't clear game_active — mode_highscore_enter uses it
                 * to gate score submission and clears it after. */
                ctx->highscore_request_type = HIGHSCORE_TYPE_PERSONAL;
                sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
                return;
            }
        }
        return;
    }

    sdl2_state_mode_t prev = sdl2_state_previous(ctx->state);

    if (prev == SDL2ST_EDIT)
    {
        /* Play-test from editor — use existing blocks, just place ball */
        ctx->attract_level_display = 0;
        ctx->lives_left = 3;
        ctx->game_active = true;
        ctx->score_submitted = false;
        ctx->game_start = time(NULL);
        ctx->paused_seconds = 0;
        paddle_system_reset(ctx->paddle);
        paddle_system_set_size(ctx->paddle, PADDLE_SIZE_HUGE);
        ball_system_clear_all(ctx->ball);
        ball_system_env_t env = game_callbacks_ball_env(ctx);
        ball_system_reset_start(ctx->ball, &env);
        gun_system_set_ammo(ctx->gun, GUN_MAX_AMMO);

        /* Display the editor-loaded level's title — without this, the
         * message bar shows whatever sticky was last set before EDIT
         * (e.g. "Save the rainforests" from INSTRUCT).  Fall back to
         * "- Untitled -" when the editor never named the level, so we
         * never silently inherit a stale sticky. */
        const char *title = level_system_get_title(ctx->level);
        char msg[80];
        if (title && title[0] != '\0')
            snprintf(msg, sizeof(msg), "- %s -", title);
        else
            snprintf(msg, sizeof(msg), "- Untitled -");
        message_system_set_default(ctx->message, msg);
        int frame = (int)sdl2_state_frame(ctx->state);
        message_system_set(ctx->message, msg, 0, frame);
    }
    else if (!ctx->game_active || prev == SDL2ST_HIGHSCORE || prev == SDL2ST_INTRO ||
             prev == SDL2ST_INSTRUCT || prev == SDL2ST_DEMO || prev == SDL2ST_KEYS ||
             prev == SDL2ST_KEYSEDIT || prev == SDL2ST_PREVIEW || prev == SDL2ST_PRESENTS)
    {
        /* Coming from attract mode or game over — start a new game */
        start_new_game(ctx);
    }
    /* Coming from pause or bonus — just resume, don't reset */

    /* Hide cursor during gameplay — paddle replaces it */
    if (ctx->cursor)
        sdl2_cursor_set(ctx->cursor, SDL2CUR_NONE);
}

static void mode_game_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    /* Input dispatch */
    game_input_update(ctx);

    /* Ball physics */
    ball_system_env_t benv = game_callbacks_ball_env(ctx);
    ball_system_update(ctx->ball, &benv);

    /* Gun physics */
    gun_system_env_t genv = game_callbacks_gun_env(ctx);
    gun_system_update(ctx->gun, &genv);

    /* Block animation slides (BONUS/DEATH/EXTRABALL/ROAMER cycling) */
    block_system_advance_animations(ctx->block, (int)sdl2_state_frame(ctx->state));

    /* Block explosion state machine — advances exploding blocks one stage
     * per tick at BLOCK_EXPLODE_DELAY=10 ticks/stage, fires the finalize
     * callback when the animation completes (matches original
     * ExplodeBlocksPending at original/blocks.c:1480-1646). */
    block_system_update_explosions(ctx->block, (int)sdl2_state_frame(ctx->state),
                                   game_callbacks_on_block_finalize, ctx);

    /* Ball→eyedude collision — original/ball.c:1339-1347 */
    game_rules_check_ball_eyedude(ctx);

    /* EyeDude character */
    eyedude_system_update(ctx->eyedude, (int)sdl2_state_frame(ctx->state), GAME_PLAY_WIDTH);

    /* SFX (shake, fade, etc.) */
    sfx_system_update(ctx->sfx, (int)sdl2_state_frame(ctx->state));
    sfx_system_update_glow(ctx->sfx, (int)sdl2_state_frame(ctx->state));
    sfx_system_update_deveyes(ctx->sfx, GAME_PLAY_WIDTH, GAME_PLAY_HEIGHT);

    /* Level timer countdown — decrement once per second.
     * At speed 5 (default), tick interval = 7500 us → ~133 ticks/sec.
     * We compute ticks-per-second from the loop speed to stay correct
     * at any speed level. */
    if (ctx->time_remaining > 0)
    {
        ctx->timer_frame_acc++;
        int speed = sdl2_loop_get_speed(ctx->loop);
        uint64_t tick_us = sdl2_loop_tick_interval_us(speed);
        int ticks_per_sec = (tick_us > 0) ? (int)(1000000ULL / tick_us) : 133;
        if (ctx->timer_frame_acc >= ticks_per_sec)
        {
            ctx->timer_frame_acc -= ticks_per_sec;
            ctx->time_remaining--;
        }
    }

    /* Message timer */
    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_update(ctx->message, frame);

    /* Game rules (level completion, bonus spawning) */
    game_rules_check(ctx);
}

/* =========================================================================
 * MODE_PAUSE — freeze everything
 * ========================================================================= */

static void mode_pause_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_set(ctx->message, "- Game paused -", 0, frame);

    /* Restore cursor while paused */
    if (ctx->cursor)
        sdl2_cursor_set(ctx->cursor, SDL2CUR_POINT);
}

static void mode_pause_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    (void)ud;
}

static void mode_pause_exit(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    /* Restore the level title (the default message) directly rather
     * than blanking the bar.  message_system_set("", auto_clear=1)
     * blanks `current` for MESSAGE_CLEAR_DELAY frames before reverting
     * to default — visible 2 s of empty bar after unpause. */
    int frame = (int)sdl2_state_frame(ctx->state);
    const char *def = message_system_get_default(ctx->message);
    message_system_set(ctx->message, def ? def : "", 0, frame);
}

/* =========================================================================
 * Attract mode frame acceleration
 *
 * The legacy game ran attract screens at sleepSync(display, 3) which
 * is about 1.2ms per frame = ~833 fps.  Our fixed-timestep loop at
 * speed 5 gives ~133 fps.  To match legacy timing, we call each
 * attract module's update() multiple times per tick, advancing the
 * module's internal frame counter faster than the real frame counter.
 *
 * ATTRACT_FRAME_MULTIPLIER controls how many virtual frames per tick.
 * ========================================================================= */

#define ATTRACT_FRAME_MULTIPLIER 6
#define ATTRACT_FLASH_INTERVAL 500
static int attract_frame_counter;
static unsigned long attract_fake_score;
static int attract_next_flash;

static void attract_random_display(game_ctx_t *ctx, int is_animating)
{
    if (!is_animating)
        return;
    if (attract_frame_counter < attract_next_flash)
        return;

    attract_next_flash = attract_frame_counter + ATTRACT_FLASH_INTERVAL;
    score_system_set_display(ctx->score, attract_fake_score++);
    ctx->attract_level_display = (rand() % LEVEL_MAX_NUM) + 1;
    special_system_randomize(ctx->special, rand);
}

/* =========================================================================
 * MODE_PRESENTS — splash screen sequence
 * ========================================================================= */

static void mode_presents_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    attract_frame_counter = 0;
    attract_next_flash = 0;
    presents_system_begin(ctx->presents, 0);
}

static void mode_presents_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        sfx_system_update_glow(ctx->sfx, attract_frame_counter);
        presents_system_update(ctx->presents, attract_frame_counter);

        presents_sound_t snd = presents_system_get_sound(ctx->presents);
        if (snd.name && ctx->audio)
            sdl2_audio_play(ctx->audio, snd.name);
    }

    /* Space skips presents */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
        presents_system_skip(ctx->presents, attract_frame_counter);

    /* on_finished callback handles the transition to intro */
}

/* =========================================================================
 * MODE_INTRO — block descriptions + sparkle
 * ========================================================================= */

static int intro_deveye_tick;
static int intro_deveye_cooldown;

static void mode_intro_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    attract_frame_counter = 0;
    attract_next_flash = 0;
    intro_deveye_tick = 0;
    intro_deveye_cooldown = 0;
    intro_system_begin(ctx->intro, INTRO_MODE_INTRO, 0);

    /* "Welcome to XBoing" in the message bar — matches
     * original/intro.c:203 SetCurrentMessage. */
    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_set_default(ctx->message, "Welcome to XBoing");
    message_system_set(ctx->message, "Welcome to XBoing", 0, frame);

    /* Start devil-eyes animation so they blink during intro
     * (original/intro.c:359 HandleBlink → BlinkDevilEyes). */
    sfx_system_start_deveyes(ctx->sfx);
}

static void mode_intro_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        sfx_system_update_glow(ctx->sfx, attract_frame_counter);
        intro_system_update(ctx->intro, attract_frame_counter);

        intro_sound_t snd = intro_system_get_sound(ctx->intro);
        if (snd.name && ctx->audio)
            sdl2_audio_play(ctx->audio, snd.name);
    }

    attract_random_display(ctx, intro_system_get_state(ctx->intro) == INTRO_STATE_EXPLODE);

    /* Devil-eyes: original BLINK_RATE=25 frames (~30ms at ~833fps),
     * BLINK_GAP=1000 frames (~1.2s).  Attract frames at 6×133tps:
     * 25 attract frames ≈ 31ms/step, 1000 attract frames ≈ 1.25s gap. */
    {
        intro_deveye_tick += ATTRACT_FRAME_MULTIPLIER;

        int still_active = sfx_system_get_deveye_info(ctx->sfx).active;
        if (still_active)
        {
            if (intro_deveye_tick >= 25)
            {
                sfx_system_update_deveyes(ctx->sfx, GAME_PLAY_WIDTH, GAME_PLAY_HEIGHT);
                intro_deveye_tick = 0;
            }
        }
        else
        {
            intro_deveye_cooldown += ATTRACT_FRAME_MULTIPLIER;
            if (intro_deveye_cooldown >= 1000)
            {
                sfx_system_start_deveyes(ctx->sfx);
                intro_deveye_cooldown = 0;
            }
        }
    }

    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
    {
        sdl2_state_transition(ctx->state, SDL2ST_GAME);
        return;
    }
}

/* =========================================================================
 * MODE_INSTRUCT — instructions text + sparkle
 * ========================================================================= */

static void mode_instruct_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    attract_frame_counter = 0;
    attract_next_flash = 0;
    intro_system_begin(ctx->intro, INTRO_MODE_INSTRUCT, 0);

    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_set(ctx->message, "Save the rainforests", 0, frame);
}

static void mode_instruct_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        sfx_system_update_glow(ctx->sfx, attract_frame_counter);
        intro_system_update(ctx->intro, attract_frame_counter);

        intro_sound_t snd = intro_system_get_sound(ctx->intro);
        if (snd.name && ctx->audio)
            sdl2_audio_play(ctx->audio, snd.name);
    }

    attract_random_display(ctx, intro_system_get_state(ctx->intro) == INTRO_STATE_EXPLODE);

    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
    {
        sdl2_state_transition(ctx->state, SDL2ST_GAME);
        return;
    }
}

/* =========================================================================
 * MODE_DEMO — gameplay illustration
 * ========================================================================= */

static void mode_demo_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    attract_frame_counter = 0;
    attract_next_flash = 0;
    demo_system_begin(ctx->demo, DEMO_MODE_DEMO, 0);

    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_set(ctx->message, "Demonstration", 0, frame);

    /* Load demo.data blocks per original/demo.c:137 */
    {
        char level_path[PATHS_MAX_PATH];
        ctx->time_bonus_total = 0;
        ctx->time_remaining = 0;
        if (paths_level_file(&ctx->paths, "demo.data", level_path, sizeof(level_path)) == PATHS_OK)
        {
            block_system_clear_all(ctx->block);
            if (level_system_load_file(ctx->level, level_path) == LEVEL_SYS_OK)
            {
                ctx->time_bonus_total = level_system_get_time_bonus(ctx->level);
                ctx->time_remaining = ctx->time_bonus_total;
            }
        }
    }
}

static void mode_demo_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        sfx_system_update_glow(ctx->sfx, attract_frame_counter);
        demo_system_update(ctx->demo, attract_frame_counter);

        demo_sound_t snd = demo_system_get_sound(ctx->demo);
        if (snd.name && ctx->audio)
            sdl2_audio_play(ctx->audio, snd.name);
    }

    attract_random_display(ctx, demo_system_get_state(ctx->demo) == DEMO_STATE_SPARKLE);

    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
    {
        sdl2_state_transition(ctx->state, SDL2ST_GAME);
        return;
    }
}

/* =========================================================================
 * MODE_PREVIEW — random level preview
 * ========================================================================= */

static int preview_message_set;

static void mode_preview_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    attract_frame_counter = 0;
    attract_next_flash = 0;
    preview_message_set = 0;
    demo_system_begin(ctx->demo, DEMO_MODE_PREVIEW, 0);
}

static void mode_preview_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        sfx_system_update_glow(ctx->sfx, attract_frame_counter);
        demo_system_update(ctx->demo, attract_frame_counter);

        demo_sound_t snd = demo_system_get_sound(ctx->demo);
        if (snd.name && ctx->audio)
            sdl2_audio_play(ctx->audio, snd.name);
    }

    attract_random_display(ctx, demo_system_get_state(ctx->demo) == DEMO_STATE_WAIT);

    if (!preview_message_set)
    {
        int level = demo_system_get_preview_level(ctx->demo);
        if (level > 0)
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "Preview of level %d", level);
            int frame = (int)sdl2_state_frame(ctx->state);
            message_system_set(ctx->message, msg, 0, frame);
            preview_message_set = 1;
        }
    }

    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
    {
        sdl2_state_transition(ctx->state, SDL2ST_GAME);
        return;
    }
}

/* =========================================================================
 * MODE_KEYS — game controls display
 * ========================================================================= */

static int keys_deveye_tick;
static int keys_deveye_cooldown;

static void mode_keys_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    attract_frame_counter = 0;
    attract_next_flash = 0;
    keys_deveye_tick = 0;
    keys_deveye_cooldown = 0;
    keys_system_begin(ctx->keys, KEYS_MODE_GAME, 0);

    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_set(ctx->message, "Drink driving kills!", 0, frame);

    sfx_system_start_deveyes(ctx->sfx);
}

static void mode_keys_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        sfx_system_update_glow(ctx->sfx, attract_frame_counter);
        keys_system_update(ctx->keys, attract_frame_counter);
    }

    attract_random_display(ctx, keys_system_get_state(ctx->keys) == KEYS_STATE_SPARKLE);

    /* Devil eyes during keys screen per original/keys.c:332 */
    {
        keys_deveye_tick += ATTRACT_FRAME_MULTIPLIER;

        int still_active = sfx_system_get_deveye_info(ctx->sfx).active;
        if (still_active)
        {
            if (keys_deveye_tick >= 25)
            {
                sfx_system_update_deveyes(ctx->sfx, GAME_PLAY_WIDTH, GAME_PLAY_HEIGHT);
                keys_deveye_tick = 0;
            }
        }
        else
        {
            keys_deveye_cooldown += ATTRACT_FRAME_MULTIPLIER;
            if (keys_deveye_cooldown >= 1000)
            {
                sfx_system_start_deveyes(ctx->sfx);
                keys_deveye_cooldown = 0;
            }
        }
    }

    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
    {
        sdl2_state_transition(ctx->state, SDL2ST_GAME);
        return;
    }
}

/* =========================================================================
 * MODE_KEYSEDIT — editor controls display
 * ========================================================================= */

static void mode_keysedit_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    attract_frame_counter = 0;
    attract_next_flash = 0;
    keys_system_begin(ctx->keys, KEYS_MODE_EDITOR, 0);

    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_set(ctx->message, "Be happy!", 0, frame);
}

static void mode_keysedit_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        sfx_system_update_glow(ctx->sfx, attract_frame_counter);
        keys_system_update(ctx->keys, attract_frame_counter);
    }

    attract_random_display(ctx, keys_system_get_state(ctx->keys) == KEYS_STATE_SPARKLE);

    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
    {
        sdl2_state_transition(ctx->state, SDL2ST_GAME);
        return;
    }
}

/* =========================================================================
 * MODE_HIGHSCORE — high score table display (attract mode cycling)
 * ========================================================================= */

/* =========================================================================
 * MODE_BONUS — bonus tally sequence between levels
 * ========================================================================= */

static void mode_bonus_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    attract_frame_counter = 0;
    attract_next_flash = 0;

    /* Restore cursor during bonus tally */
    if (ctx->cursor)
        sdl2_cursor_set(ctx->cursor, SDL2CUR_POINT);

    unsigned long score_val = score_system_get(ctx->score);
    /* DoHighScore on the bonus screen — original/bonus.c:528-579 calls
     * GetHighScoreRanking which reads GLOBAL (highscore.c:622-640).
     * Re-read from disk first so other users' recent inserts are
     * visible (matches the wisdom-prompt path in mode_highscore_enter).
     * Read into a stack-local then swap on OK — highscore_io_read
     * zeroes the destination on EOPEN/EREAD/EVERSION, so the in-memory
     * cache must not be the destination on a transient read failure. */
    {
        char global_path[PATHS_MAX_PATH];
        if (paths_score_file_global(&ctx->paths, global_path, sizeof(global_path)) == PATHS_OK)
        {
            highscore_table_t fresh;
            if (highscore_io_read(global_path, &fresh) == HIGHSCORE_IO_OK)
                ctx->hs_global = fresh;
        }
    }
    int rank = highscore_io_get_ranking(&ctx->hs_global, score_val);

    bonus_system_env_t env = {
        .score = score_val,
        .level = ctx->level_number,
        .starting_level = ctx->start_level,
        .time_bonus_secs = ctx->time_remaining,
        .bullet_count = gun_system_get_ammo(ctx->gun),
        .highscore_rank = rank,
    };

    /* Set coins BEFORE begin — bonus_system_begin captures
     * initial_coin_count and computes the bonus total from
     * ctx->coin_count at that point (bonus_system.c:189, 196).
     * Reversing this order silently produces wrong initial
     * counts AND a wrong score total.  See ADR-040. */
    bonus_system_set_coins(ctx->bonus, ctx->bonus_count);
    bonus_system_begin(ctx->bonus, &env, 0);

    /* "- Bonus Tally -" persists through the entire sequence
     * (original/bonus.c:232 — DrawTitleText calls SetCurrentMessage
     * with UpdateFlag=True, meaning show immediately and don't
     * clear until replaced).  auto_clear=0 matches. */
    message_system_set(ctx->message, "- Bonus Tally -", 0, attract_frame_counter);
}

static void mode_bonus_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        sfx_system_update_glow(ctx->sfx, attract_frame_counter);
        bonus_system_update(ctx->bonus, attract_frame_counter);
        /* Stop pumping once the sequence has signalled completion.
         * do_finish is already guarded against re-entry, but skipping
         * the rest of the burst avoids the no-op call chain through
         * the state machine and keeps the loop honest. */
        if (bonus_system_is_finished(ctx->bonus))
        {
            break;
        }
    }

    /* Space skips the bonus tally */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
        bonus_system_skip(ctx->bonus, attract_frame_counter);

    /* on_finished callback handles transition to next level */
}

/* =========================================================================
 * Player name resolution — matches legacy getUsersFullName() in misc.c
 * ========================================================================= */

static const char *get_player_name(const game_ctx_t *ctx)
{
    static char fullname[80];

    /* Prefer configured nickname */
    if (ctx->config.nickname[0] != '\0')
        return ctx->config.nickname;

    /* Try real name from passwd gecos field */
    const struct passwd *pw = getpwuid(getuid());
    if (pw != NULL && pw->pw_gecos != NULL && pw->pw_gecos[0] != '\0')
    {
        /* pw_gecos may contain comma-separated fields; use only the name */
        strncpy(fullname, pw->pw_gecos, sizeof(fullname) - 1);
        fullname[sizeof(fullname) - 1] = '\0';
        char *comma = strchr(fullname, ',');
        if (comma != NULL)
            *comma = '\0';
        if (fullname[0] != '\0')
            return fullname;
    }

    /* Fall back to login name (copy — getpwuid returns static buffer) */
    if (pw != NULL && pw->pw_name != NULL && pw->pw_name[0] != '\0')
    {
        strncpy(fullname, pw->pw_name, sizeof(fullname) - 1);
        fullname[sizeof(fullname) - 1] = '\0';
        return fullname;
    }

    /* Last resort */
    const char *user = getenv("USER");
    if (user != NULL && user[0] != '\0')
    {
        strncpy(fullname, user, sizeof(fullname) - 1);
        fullname[sizeof(fullname) - 1] = '\0';
        return fullname;
    }
    return "Player";
}

/* =========================================================================
 * MODE_HIGHSCORE — high score table display (attract mode cycling)
 * ========================================================================= */

/* Submit a finished game's score to both the personal and global tables.
 *
 * master_text is passed only into the global insert, which applies it
 * iff the new entry lands at rank 0 — mirroring original/highscore.c:747
 * (SetBoingMasterText inside the GLOBAL branch).  Pass NULL when the
 * player isn't the new boing master or cancelled the dialogue.
 *
 * Return value contract — narrow on purpose, do NOT use as a generic
 * success indicator:
 *
 *   1: the GLOBAL atomic insert returned HIGHSCORE_IO_OK (player ranked
 *      on the Hall of Fame).  Personal insert/write status is NOT
 *      reflected here — they may have failed independently.
 *   0: the global insert was rejected (NOT_RANKED, file resolve
 *      failed, elevate failed, or any other error).
 *
 * Callers use the return only to decide which post-game table to
 * display (matches original/level.c:446-449 ResetHighScore(PERSONAL)
 * on global rejection).  Failure modes other than "did the player
 * place globally" are surfaced via stderr inside this function; they
 * do not affect the return value. */
static int submit_score(game_ctx_t *ctx, unsigned long score, unsigned long game_time,
                        unsigned long ts, const char *name, const char *master_text)
{
    /* Personal table — in-memory insert, then disk write.  Log every
     * failure: silent loss on game-over is unrecoverable. */
    highscore_io_result_t ins = highscore_io_insert(
        &ctx->hs_personal, score, (unsigned long)ctx->level_number, game_time, ts, name);
    if (ins == HIGHSCORE_IO_OK)
    {
        char score_path[PATHS_MAX_PATH];
        if (paths_score_file_personal(&ctx->paths, score_path, sizeof(score_path)) != PATHS_OK)
        {
            fprintf(stderr, "xboing: personal high score path resolve failed; entry kept in "
                            "memory only\n");
        }
        else
        {
            highscore_io_result_t pwr = highscore_io_write(score_path, &ctx->hs_personal);
            if (pwr != HIGHSCORE_IO_OK)
            {
                fprintf(stderr, "xboing: personal high score write to %s failed (code %d)\n",
                        score_path, (int)pwr);
            }
        }
    }

    /* Global table — atomic per-uid-deduped insert under flock,
     * elevated to egid=games for the /var/games/xboing write. */
    int global_ok = 0;
    char global_path[PATHS_MAX_PATH];
    if (paths_score_file_global(&ctx->paths, global_path, sizeof(global_path)) != PATHS_OK)
    {
        fprintf(stderr, "xboing: global high score path resolve failed; skipping global insert\n");
    }
    else if (sys_priv_elevate() != 0)
    {
        fprintf(stderr, "xboing: cannot elevate to write global high score; skipping\n");
    }
    else
    {
        highscore_io_result_t gres = highscore_io_insert_global_atomic(
            global_path, score, (unsigned long)ctx->level_number, game_time, ts,
            (unsigned long)getuid(), name, master_text);
        sys_priv_drop();

        /* Refresh in-memory copy on success AND on NOT_RANKED (the
         * locked re-read inside _atomic may have surfaced other users'
         * recent inserts; reflect them in the next display).  Skip
         * refresh only on I/O errors that left disk untouched.  Swap
         * on OK only — highscore_io_read zeroes its destination on
         * failure and we'd rather keep the previous in-memory copy. */
        if (gres == HIGHSCORE_IO_OK || gres == HIGHSCORE_IO_ERR_NOT_RANKED)
        {
            highscore_table_t fresh;
            if (highscore_io_read(global_path, &fresh) == HIGHSCORE_IO_OK)
                ctx->hs_global = fresh;
        }
        if (gres == HIGHSCORE_IO_OK)
        {
            global_ok = 1;
        }
        else if (gres != HIGHSCORE_IO_ERR_NOT_RANKED)
        {
            fprintf(stderr, "xboing: global high score insert to %s failed (code %d)\n",
                    global_path, (int)gres);
        }
    }
    return global_ok;
}

static void mode_highscore_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    attract_frame_counter = 0;
    attract_next_flash = 0;

    /* Restore cursor when leaving gameplay */
    if (ctx->cursor)
        sdl2_cursor_set(ctx->cursor, SDL2CUR_POINT);

    /* Did we just submit a score (game-over path)?  Mode_highscore_enter
     * runs in two scenarios per game over: (1) wisdom_pending after the
     * dialog closes, (2) fresh entry from game_rules_ball_died.  Either
     * way, when the submission is from a real game-over we want to flip
     * to PERSONAL if the global insert was rejected — matching
     * original/level.c:446-449 which does ResetHighScore(PERSONAL) on
     * global-insert failure. */
    int just_submitted = 0;
    int global_ok = 0;

    /* Returning from "words of wisdom" dialogue — finalize the deferred
     * insert using the captured wisdom text (NULL/empty when cancelled). */
    if (wisdom_pending)
    {
        wisdom_pending = 0;
        const char *wisdom = NULL;
        if (!dialogue_system_was_cancelled(ctx->dialogue))
            wisdom = dialogue_system_get_input(ctx->dialogue);
        global_ok = submit_score(ctx, pending_final_score, pending_game_time, pending_ts,
                                 pending_name, wisdom);
        ctx->score_submitted = true;
        just_submitted = 1;
        /* Fall through to display */
    }
    else if (!ctx->score_submitted && ctx->game_active)
    {
        unsigned long final_score = score_system_get(ctx->score);
        if (final_score > 0)
        {
            unsigned long game_time = 0;
            if (ctx->game_start > 0)
            {
                time_t now = time(NULL);
                unsigned long elapsed =
                    (now >= ctx->game_start) ? (unsigned long)(now - ctx->game_start) : 0;
                unsigned long paused = (unsigned long)ctx->paused_seconds;
                game_time = (elapsed > paused) ? elapsed - paused : 0;
            }

            const char *name = get_player_name(ctx);
            unsigned long ts = (unsigned long)time(NULL);

            /* Boing-master prompt — original gates on global rank
             * (highscore.c:622-640 reads GLOBAL inside GetHighScoreRanking,
             * called from level.c:434 before either insert).  Re-read
             * from disk so another user's recent insert is visible.
             * Swap-on-OK so a transient read failure can't wipe the
             * in-memory cache (highscore_io_read zeroes its destination
             * on EOPEN/EREAD/EVERSION). */
            {
                char global_path[PATHS_MAX_PATH];
                if (paths_score_file_global(&ctx->paths, global_path, sizeof(global_path)) ==
                    PATHS_OK)
                {
                    highscore_table_t fresh;
                    if (highscore_io_read(global_path, &fresh) == HIGHSCORE_IO_OK)
                        ctx->hs_global = fresh;
                }
            }

            /* Use dedup-aware helper: prompts only when the locked
             * insert will land us at rank 0, not when our existing
             * higher score would block insertion. */
            if (highscore_io_would_be_global_master(&ctx->hs_global, final_score,
                                                    (unsigned long)getuid()) &&
                sdl2_state_push_dialogue(ctx->state) == SDL2ST_OK)
            {
                pending_final_score = final_score;
                pending_game_time = game_time;
                pending_ts = ts;
                strncpy(pending_name, name, sizeof(pending_name) - 1);
                pending_name[sizeof(pending_name) - 1] = '\0';
                dialogue_system_open(ctx->dialogue, "Words of wisdom Boing Master?",
                                     DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_TEXT);
                wisdom_pending = 1;
                return; /* Defer inserts until dialogue closes */
            }

            global_ok = submit_score(ctx, final_score, game_time, ts, name, NULL);
            ctx->score_submitted = true;
            just_submitted = 1;
        }
    }

    /* Post-game default table choice — original/level.c:446-449.  When
     * the global insert was rejected (per-uid dedup or non-ranking
     * score), show PERSONAL so the player sees their own progress
     * instead of an irrelevant Hall of Fame. */
    if (just_submitted && !global_ok)
    {
        ctx->highscore_request_type = HIGHSCORE_TYPE_PERSONAL;
    }

    highscore_type_t type = ctx->highscore_request_type;
    const highscore_table_t *table =
        (type == HIGHSCORE_TYPE_GLOBAL) ? &ctx->hs_global : &ctx->hs_personal;
    highscore_system_set_table(ctx->highscore_display, table);
    highscore_system_set_current_score(ctx->highscore_display, score_system_get(ctx->score));
    highscore_system_begin(ctx->highscore_display, type, 0);

    /* Don't overwrite the in-game message bar when we arrived from
     * game-over — game_rules_ball_died set "GAME OVER" and the player
     * should see it (matches original/level.c:455 which sets
     * "- Game Over - " in EndTheGame; original's highscore display
     * never touches the message bar).  Gate on game_active, which
     * stays true through the entire game-over highscore display
     * (re-entries from dialog pop included) and is cleared by
     * mode_highscore_update on START.  In the attract-cycle case
     * game_active is false and the label tells the player which view
     * they're on. */
    if (!ctx->game_active)
    {
        int frame = (int)sdl2_state_frame(ctx->state);
        const char *label =
            (type == HIGHSCORE_TYPE_GLOBAL) ? "<h> - Hall of Fame" : "<H> - Personal Best";
        message_system_set(ctx->message, label, 0, frame);
    }
}

static void mode_highscore_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        sfx_system_update_glow(ctx->sfx, attract_frame_counter);
        highscore_system_update(ctx->highscore_display, attract_frame_counter);
    }

    attract_random_display(ctx, highscore_system_get_state(ctx->highscore_display) ==
                                    HIGHSCORE_STATE_SPARKLE);

    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
    {
        /* If came from game over, start new game; if attract mode, go to intro */
        if (ctx->game_active)
        {
            ctx->game_active = false;
            sdl2_state_transition(ctx->state, SDL2ST_INTRO);
        }
        else
        {
            sdl2_state_transition(ctx->state, SDL2ST_GAME);
        }
        return;
    }

    /* on_finished callback handles auto-cycle */
}

/* =========================================================================
 * MODE_DIALOGUE — modal text input overlay (push/pop)
 * ========================================================================= */

static void mode_dialogue_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    (void)ud;
    SDL_StartTextInput();
}

static void mode_dialogue_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    /* Read sound before update — update() clears the sound field
     * (dialogue_system.c:113-114), but key_input() sets it during
     * event processing before this tick runs. */
    dialogue_sound_t snd = dialogue_system_get_sound(ctx->dialogue);
    if (snd.name && ctx->audio)
        sdl2_audio_play(ctx->audio, snd.name);

    dialogue_system_update(ctx->dialogue);

    if (dialogue_system_is_finished(ctx->dialogue))
    {
        sdl2_state_pop_dialogue(ctx->state);
    }
}

static void mode_dialogue_exit(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    SDL_StopTextInput();

    if (quit_pending)
    {
        quit_pending = 0;
        if (!dialogue_system_was_cancelled(ctx->dialogue))
        {
            const char *ans = dialogue_system_get_input(ctx->dialogue);
            if (ans && (ans[0] == 'y' || ans[0] == 'Y'))
            {
                SDL_Event quit_event = {0};
                quit_event.type = SDL_QUIT;
                SDL_PushEvent(&quit_event);
            }
        }
    }

    /* W key: set starting level — original/level.c:245-282 */
    if (level_pending)
    {
        level_pending = 0;
        if (!dialogue_system_was_cancelled(ctx->dialogue))
        {
            const char *ans = dialogue_system_get_input(ctx->dialogue);
            if (ans && ans[0] != '\0')
            {
                int num = atoi(ans);
                int frame = (int)sdl2_state_frame(ctx->state);
                if (num > 0 && num <= LEVEL_MAX_NUM)
                {
                    ctx->start_level = num;
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Starting level set to %d", num);
                    message_system_set(ctx->message, msg, 1, frame);
                }
                else
                {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "Invalid - level range [1-%d]", LEVEL_MAX_NUM);
                    message_system_set(ctx->message, msg, 1, frame);
                }
            }
        }
    }
}

/* =========================================================================
 * MODE_EDIT — level editor
 * ========================================================================= */

static void mode_edit_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    /* Restore cursor for editor interaction */
    if (ctx->cursor)
        sdl2_cursor_set(ctx->cursor, SDL2CUR_PLUS);

    editor_system_reset(ctx->editor);
    editor_system_init_palette(ctx->editor, MAX_STATIC_BLOCKS);
    /* Don't clear blocks here — editor_system's do_load_level handles loading.
     * The on_load_level callback calls block_system_clear_all before loading. */
}

static void mode_edit_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    int frame = (int)sdl2_state_frame(ctx->state);
    editor_system_update(ctx->editor, frame);

    /* Mouse input — translate window coords to play area coords */
    int mx = 0, my = 0;
    sdl2_input_get_mouse(ctx->input, &mx, &my);
    int play_x = mx - PLAY_AREA_X;
    int play_y = my - PLAY_AREA_Y;

    /* Mouse buttons: left=draw, middle or right=erase */
    if (sdl2_input_mouse_pressed(ctx->input, 1))
        editor_system_mouse_button(ctx->editor, play_x, play_y, 1, 1);
    else
        editor_system_mouse_button(ctx->editor, play_x, play_y, 1, 0);

    if (sdl2_input_mouse_pressed(ctx->input, 2) || sdl2_input_mouse_pressed(ctx->input, 3))
        editor_system_mouse_button(ctx->editor, play_x, play_y, 2, 1);

    /* Mouse drag */
    editor_system_mouse_motion(ctx->editor, play_x, play_y);

    /* Keyboard commands */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_QUIT) ||
        sdl2_input_just_pressed(ctx->input, SDL2I_ABORT))
    {
        /* Try editor's quit flow first (sets state to FINISH) */
        editor_system_key_input(ctx->editor, EDITOR_KEY_QUIT);
        /* If the editor didn't handle it (state unchanged), force exit */
        if (editor_system_get_state(ctx->editor) != EDITOR_STATE_FINISH)
            sdl2_state_transition(ctx->state, SDL2ST_INTRO);
    }
    /* Editor keys match legacy editor.c:handleAllEditorKeys():
     * P=playtest, S=save, L=load, C=clear, T=time, N=name, R=redraw
     * h=flip-h, H=scroll-h, v=flip-v, V=scroll-v
     *
     * Uses raw SDL scancodes because many of these letters are bound to
     * game actions (L=right, S=sfx toggle) in the input binding table. */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_PAUSE)) /* P */
        editor_system_key_input(ctx->editor, EDITOR_KEY_PLAYTEST);

    {
        static Uint32 ed_last[SDL_NUM_SCANCODES];
        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        Uint32 now = SDL_GetTicks();
        int shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];

#define ED_KEY(sc, cmd)                                                                            \
    if (keys[(sc)] && (now - ed_last[(sc)] > 300))                                                 \
    {                                                                                              \
        ed_last[(sc)] = now;                                                                       \
        editor_system_key_input(ctx->editor, (cmd));                                               \
    }

        ED_KEY(SDL_SCANCODE_S, EDITOR_KEY_SAVE)
        ED_KEY(SDL_SCANCODE_L, EDITOR_KEY_LOAD)
        ED_KEY(SDL_SCANCODE_C, EDITOR_KEY_CLEAR)
        ED_KEY(SDL_SCANCODE_T, EDITOR_KEY_TIME)
        ED_KEY(SDL_SCANCODE_N, EDITOR_KEY_NAME)
        ED_KEY(SDL_SCANCODE_R, EDITOR_KEY_REDRAW)

        if (!shift)
        {
            ED_KEY(SDL_SCANCODE_H, EDITOR_KEY_FLIP_H)
            ED_KEY(SDL_SCANCODE_V, EDITOR_KEY_FLIP_V)
        }
        else
        {
            ED_KEY(SDL_SCANCODE_H, EDITOR_KEY_SCROLL_H)
            ED_KEY(SDL_SCANCODE_V, EDITOR_KEY_SCROLL_V)
        }

#undef ED_KEY
    }

    /* Palette selection: keys 1-9 select palette entries 0-8 */
    for (int s = 1; s <= 9; s++)
    {
        sdl2_input_action_t action = (sdl2_input_action_t)(SDL2I_SPEED_1 + s - 1);
        if (sdl2_input_just_pressed(ctx->input, action))
        {
            editor_system_select_palette(ctx->editor, s - 1);
            break;
        }
    }

    /* Palette selection via left/right arrows to cycle */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_RIGHT))
    {
        int sel = editor_system_get_selected_palette(ctx->editor);
        int count = editor_system_get_palette_count(ctx->editor);
        editor_system_select_palette(ctx->editor, (sel + 1) % count);
    }
    if (sdl2_input_just_pressed(ctx->input, SDL2I_LEFT))
    {
        int sel = editor_system_get_selected_palette(ctx->editor);
        int count = editor_system_get_palette_count(ctx->editor);
        editor_system_select_palette(ctx->editor, (sel - 1 + count) % count);
    }

    /* Palette selection via mouse click on sidebar (x > play area right edge) */
    {
        int palette_x = PLAY_AREA_X + 495 + 15; /* PALETTE_X from game_render.c */
        /* cppcheck-suppress variableScope ; kept local to this block for clarity */
        int palette_entry_h = 25;
        if (mx > palette_x && sdl2_input_mouse_pressed(ctx->input, 1))
        {
            int idx = (my - PLAY_AREA_Y) / palette_entry_h;
            int count = editor_system_get_palette_count(ctx->editor);
            if (idx >= 0 && idx < count)
                editor_system_select_palette(ctx->editor, idx);
        }
    }
}

/* =========================================================================
 * Registration
 * ========================================================================= */

void game_modes_register(game_ctx_t *ctx)
{
    /* MODE_GAME */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_game_enter,
            .on_update = mode_game_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_GAME, &def);
    }

    /* MODE_PAUSE */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_pause_enter,
            .on_update = mode_pause_update,
            .on_exit = mode_pause_exit,
        };
        sdl2_state_register(ctx->state, SDL2ST_PAUSE, &def);
    }

    /* MODE_PRESENTS */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_presents_enter,
            .on_update = mode_presents_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_PRESENTS, &def);
    }

    /* MODE_INTRO */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_intro_enter,
            .on_update = mode_intro_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_INTRO, &def);
    }

    /* MODE_INSTRUCT */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_instruct_enter,
            .on_update = mode_instruct_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_INSTRUCT, &def);
    }

    /* MODE_DEMO */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_demo_enter,
            .on_update = mode_demo_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_DEMO, &def);
    }

    /* MODE_PREVIEW */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_preview_enter,
            .on_update = mode_preview_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_PREVIEW, &def);
    }

    /* MODE_KEYS */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_keys_enter,
            .on_update = mode_keys_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_KEYS, &def);
    }

    /* MODE_KEYSEDIT */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_keysedit_enter,
            .on_update = mode_keysedit_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_KEYSEDIT, &def);
    }

    /* MODE_HIGHSCORE (stub — full rendering in bead 3.5) */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_highscore_enter,
            .on_update = mode_highscore_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_HIGHSCORE, &def);
    }

    /* MODE_BONUS */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_bonus_enter,
            .on_update = mode_bonus_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_BONUS, &def);
    }

    /* MODE_EDIT */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_edit_enter,
            .on_update = mode_edit_update,
        };
        sdl2_state_register(ctx->state, SDL2ST_EDIT, &def);
    }

    /* MODE_DIALOGUE */
    {
        sdl2_state_mode_def_t def = {
            .on_enter = mode_dialogue_enter,
            .on_update = mode_dialogue_update,
            .on_exit = mode_dialogue_exit,
        };
        sdl2_state_register(ctx->state, SDL2ST_DIALOGUE, &def);
    }
}
