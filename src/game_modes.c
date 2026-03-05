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

#include <SDL2/SDL.h>

#include "ball_system.h"
#include "block_system.h"
#include "bonus_system.h"
#include "demo_system.h"
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
#include "sdl2_input.h"
#include "sdl2_state.h"
#include "sfx_system.h"
#include "special_system.h"

/* =========================================================================
 * MODE_GAME — core gameplay
 * ========================================================================= */

/* Play area constants */
#define GAME_PLAY_WIDTH 495
#define GAME_PLAY_HEIGHT 580
#define GAME_COL_WIDTH (GAME_PLAY_WIDTH / 9)
#define GAME_ROW_HEIGHT (GAME_PLAY_HEIGHT / 18)

/* Play area position in window (from legacy stage.c) */
#define PLAY_AREA_X 35
#define PLAY_AREA_Y 60

static void start_new_game(game_ctx_t *ctx)
{
    /* Reset game state */
    ctx->level_number = ctx->start_level;
    ctx->lives_left = 3;
    ctx->game_active = true;
    ctx->bonus_block_active = false;
    ctx->next_bonus_frame = 0;
    ctx->user_tilts = 0;

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

    /* Set level title as default message */
    const char *title = level_system_get_title(ctx->level);
    if (title)
        message_system_set_default(ctx->message, title);

    /* Place ball on paddle */
    ball_system_env_t env = game_callbacks_ball_env(ctx);
    ball_system_reset_start(ctx->ball, &env);

    if (ctx->audio)
        sdl2_audio_play(ctx->audio, "newlevel");
}

static void mode_game_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    sdl2_state_mode_t prev = sdl2_state_previous(ctx->state);

    if (prev == SDL2ST_EDIT)
    {
        /* Play-test from editor — use existing blocks, just place ball */
        ctx->lives_left = 3;
        ctx->game_active = true;
        paddle_system_reset(ctx->paddle);
        paddle_system_set_size(ctx->paddle, PADDLE_SIZE_HUGE);
        ball_system_clear_all(ctx->ball);
        ball_system_env_t env = game_callbacks_ball_env(ctx);
        ball_system_reset_start(ctx->ball, &env);
        gun_system_set_ammo(ctx->gun, GUN_MAX_AMMO);
    }
    else if (!ctx->game_active || prev == SDL2ST_HIGHSCORE || prev == SDL2ST_INTRO ||
             prev == SDL2ST_INSTRUCT || prev == SDL2ST_DEMO || prev == SDL2ST_KEYS ||
             prev == SDL2ST_KEYSEDIT || prev == SDL2ST_PREVIEW || prev == SDL2ST_PRESENTS)
    {
        /* Coming from attract mode or game over — start a new game */
        start_new_game(ctx);
    }
    /* Coming from pause or bonus — just resume, don't reset */
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

    /* EyeDude character */
    eyedude_system_update(ctx->eyedude, (int)sdl2_state_frame(ctx->state), GAME_PLAY_WIDTH);

    /* SFX (shake, fade, etc.) */
    sfx_system_update(ctx->sfx, (int)sdl2_state_frame(ctx->state));
    sfx_system_update_glow(ctx->sfx, (int)sdl2_state_frame(ctx->state));
    sfx_system_update_deveyes(ctx->sfx, GAME_PLAY_WIDTH, GAME_PLAY_HEIGHT);

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
}

static void mode_pause_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    /* Only check for unpause */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_PAUSE))
        sdl2_state_transition(ctx->state, SDL2ST_GAME);
}

static void mode_pause_exit(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    int frame = (int)sdl2_state_frame(ctx->state);
    message_system_set(ctx->message, "", 1, frame);
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
static int attract_frame_counter;

/* =========================================================================
 * MODE_PRESENTS — splash screen sequence
 * ========================================================================= */

static void mode_presents_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    attract_frame_counter = 0;
    presents_system_begin(ctx->presents, 0);
}

static void mode_presents_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        presents_system_update(ctx->presents, attract_frame_counter);
    }

    /* Space skips presents */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
        presents_system_skip(ctx->presents, attract_frame_counter);

    /* on_finished callback handles the transition to intro */
}

/* =========================================================================
 * MODE_INTRO — block descriptions + sparkle
 * ========================================================================= */

static void mode_intro_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    attract_frame_counter = 0;
    intro_system_begin(ctx->intro, INTRO_MODE_INTRO, 0);
}

static void mode_intro_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        intro_system_update(ctx->intro, attract_frame_counter);
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
    intro_system_begin(ctx->intro, INTRO_MODE_INSTRUCT, 0);
}

static void mode_instruct_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        intro_system_update(ctx->intro, attract_frame_counter);
    }

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
    demo_system_begin(ctx->demo, DEMO_MODE_DEMO, 0);
}

static void mode_demo_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        demo_system_update(ctx->demo, attract_frame_counter);
    }

    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
    {
        sdl2_state_transition(ctx->state, SDL2ST_GAME);
        return;
    }
}

/* =========================================================================
 * MODE_PREVIEW — random level preview
 * ========================================================================= */

static void mode_preview_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    attract_frame_counter = 0;
    demo_system_begin(ctx->demo, DEMO_MODE_PREVIEW, 0);
}

static void mode_preview_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        demo_system_update(ctx->demo, attract_frame_counter);
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

static void mode_keys_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    attract_frame_counter = 0;
    keys_system_begin(ctx->keys, KEYS_MODE_GAME, 0);
}

static void mode_keys_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        keys_system_update(ctx->keys, attract_frame_counter);
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
    keys_system_begin(ctx->keys, KEYS_MODE_EDITOR, 0);
}

static void mode_keysedit_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        keys_system_update(ctx->keys, attract_frame_counter);
    }

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

    unsigned long score_val = score_system_get(ctx->score);
    int rank = highscore_io_get_ranking(&ctx->hs_personal, score_val);

    bonus_system_env_t env = {
        .score = score_val,
        .level = ctx->level_number,
        .starting_level = ctx->start_level,
        .time_bonus_secs = 0, /* TODO: wire time bonus */
        .bullet_count = gun_system_get_ammo(ctx->gun),
        .highscore_rank = rank,
    };
    bonus_system_begin(ctx->bonus, &env, 0);
}

static void mode_bonus_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        bonus_system_update(ctx->bonus, attract_frame_counter);
    }

    /* Space skips the bonus tally */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
        bonus_system_skip(ctx->bonus, attract_frame_counter);

    /* on_finished callback handles transition to next level */
}

/* =========================================================================
 * MODE_HIGHSCORE — high score table display (attract mode cycling)
 * ========================================================================= */

static void mode_highscore_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
    attract_frame_counter = 0;

    highscore_system_set_table(ctx->highscore_display, &ctx->hs_personal);
    highscore_system_set_current_score(ctx->highscore_display, score_system_get(ctx->score));
    highscore_system_begin(ctx->highscore_display, HIGHSCORE_TYPE_PERSONAL, 0);
}

static void mode_highscore_update(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;

    for (int i = 0; i < ATTRACT_FRAME_MULTIPLIER; i++)
    {
        attract_frame_counter++;
        highscore_system_update(ctx->highscore_display, attract_frame_counter);
    }

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
 * MODE_EDIT — level editor
 * ========================================================================= */

static void mode_edit_enter(sdl2_state_mode_t mode, void *ud)
{
    (void)mode;
    game_ctx_t *ctx = ud;
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
}
