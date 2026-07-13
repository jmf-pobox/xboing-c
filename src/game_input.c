/*
 * game_input.c -- Input dispatch for SDL2-based XBoing.
 *
 * Translates sdl2_input action queries into game module calls.
 * Called once per frame after event processing, before the game tick.
 *
 * The input layer reads action state (pressed, just_pressed) and
 * dispatches to the appropriate game modules.  It does NOT modify
 * game state directly — modules handle their own state changes.
 */

#include "game_input.h"
#include "game_callbacks.h"
#include "game_modes.h"

#include <stdio.h>

#include <SDL2/SDL.h>

#include "ball_system.h"
#include "block_system.h"
#include "dialogue_system.h"
#include "editor_system.h"
#include "gun_system.h"
#include "level_system.h"
#include "message_system.h"
#include "paddle_system.h"
#include "paths.h"
#include "savegame_system.h"
#include "score_system.h"
#include "sdl2_audio.h"
#include "sdl2_input.h"
#include "sdl2_loop.h"
#include "sdl2_renderer.h"
#include "sdl2_state.h"
#include "sfx_system.h"
#include "special_system.h"

/* =========================================================================
 * Paddle input — keyboard direction + mouse position
 * ========================================================================= */

static void input_update_paddle(game_ctx_t *ctx)
{
    int direction = PADDLE_DIR_NONE;
    int mx = 0;

    if (ctx->config.use_keys)
    {
        /* Keyboard mode: direction only, no mouse — original/main.c:185-199. */
        if (sdl2_input_pressed(ctx->input, SDL2I_LEFT))
            direction = PADDLE_DIR_LEFT;
        else if (sdl2_input_pressed(ctx->input, SDL2I_RIGHT))
            direction = PADDLE_DIR_RIGHT;
    }
    else
    {
        /* Mouse mode: position only, no keyboard — original/main.c:201-213. */
        int my = 0;
        sdl2_input_get_mouse(ctx->input, &mx, &my);
    }

    paddle_system_update(ctx->paddle, direction, mx);
}

/* =========================================================================
 * Ball launch — space bar or mouse click fires the ball
 * ========================================================================= */

static void input_launch_ball(game_ctx_t *ctx)
{
    /* Space activates a waiting ball (legacy also uses mouse click —
     * that will be wired when mouse edge triggers are available) */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
    {
        ball_system_env_t env = game_callbacks_ball_env(ctx);
        ball_system_activate_waiting(ctx->ball, &env);
    }
}

/* Save/load game state — Z saves, X loads.  Delegates to
 * savegame_system which captures/restores full mid-level state. */

/* =========================================================================
 * Public API
 * ========================================================================= */

void game_input_update(game_ctx_t *ctx)
{
    sdl2_state_mode_t mode = sdl2_state_current(ctx->state);

    if (mode == SDL2ST_GAME)
    {
        input_update_paddle(ctx);
        input_launch_ball(ctx);
    }
}

/* =========================================================================
 * Global input — mode-independent keys
 *
 * Matches original/main.c handleMiscKeys (lines 814-872) +
 * handleSpeedKeys (lines 741-803).  Called from ALL modes via
 * default-case fallthrough in the original.
 *
 * Called once per visual frame from game_main.c, NOT from stub_tick
 * (which fires multiple times per frame at high speed levels,
 * causing toggle keys to multi-fire and net to zero).
 * ========================================================================= */

static const char *warp_message(int speed)
{
    switch (speed)
    {
        case 1:
            return "Warp 1 - Slow";
        case 5:
            return "Warp 5 - Medium";
        case 9:
            return "Warp 9 - Fast";
        default:
        {
            static char buf[16];
            snprintf(buf, sizeof(buf), "Warp %d", speed);
            return buf;
        }
    }
}

void game_input_global(game_ctx_t *ctx)
{
    int frame = (int)sdl2_state_frame(ctx->state);
    sdl2_state_mode_t mode = sdl2_state_current(ctx->state);

    /* Attract-mode allowlist — matches original/main.c:898-905.
     * S key (SFX) and 1-9 (speed) only fire from these modes. */
    int is_attract = (mode == SDL2ST_INTRO || mode == SDL2ST_INSTRUCT || mode == SDL2ST_DEMO ||
                      mode == SDL2ST_PREVIEW || mode == SDL2ST_KEYS || mode == SDL2ST_KEYSEDIT ||
                      mode == SDL2ST_HIGHSCORE || mode == SDL2ST_BONUS);

    /* P: pause/unpause — handled here (once per frame) not in
     * game_input_update (per tick) to prevent multi-tick toggle.
     * During play-test, P ends the test instead of pausing — matches
     * FinishPlayTest (original/editor.c:994-1001). */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_PAUSE))
    {
        if (mode == SDL2ST_GAME && ctx->play_test_active)
        {
            editor_system_key_input(ctx->editor, EDITOR_KEY_PLAYTEST);
            /* Prevents the same-frame mode_edit_update from re-processing
             * the key (xboing-1ir). */
            sdl2_input_consume(ctx->input, SDL2I_PAUSE);
            /* The transition above already moved the state machine to
             * SDL2ST_EDIT, so the local `mode` snapshot is stale for the
             * rest of this frame. Stop here rather than let later
             * GAME-gated handlers below run against the just-restored
             * editor board. */
            return;
        }
        else if (mode == SDL2ST_GAME)
            sdl2_state_transition(ctx->state, SDL2ST_PAUSE);
        else if (mode == SDL2ST_PAUSE)
            sdl2_state_transition(ctx->state, SDL2ST_GAME);
    }

    /* S: toggle visual SFX — original/main.c:639 handleIntroKeys */
    if (is_attract && sdl2_input_just_pressed(ctx->input, SDL2I_TOGGLE_SFX))
    {
        int was = sfx_system_get_enabled(ctx->sfx);
        sfx_system_set_enabled(ctx->sfx, !was);
        message_system_set(ctx->message, was ? "- SFX OFF -" : "- SFX ON -", 1, frame);
    }

    /* 1-9: set warp speed — original/main.c:741 handleSpeedKeys */
    if (is_attract)
    {
        for (int s = 1; s <= 9; s++)
        {
            sdl2_input_action_t action = (sdl2_input_action_t)(SDL2I_SPEED_1 + s - 1);
            if (sdl2_input_just_pressed(ctx->input, action))
            {
                sdl2_loop_set_speed(ctx->loop, s);
                message_system_set(ctx->message, warp_message(s), 1, frame);
                /* Per-key volume reproduces original/main.c:741-806
                 * handleSpeedKeys — playSoundFile("tone", s*10).  On
                 * Sun this scaled audio_set_play_gain; see
                 * original/audio/SUNaudio.c:243. */
                if (ctx->audio)
                    sdl2_audio_play_at_percent(ctx->audio, "tone", s * 10);
                break;
            }
        }
    }

    /* A: toggle audio on/off — original/main.c:396-422 handleSoundKey.
     * Global key (works in both attract and gameplay). */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_TOGGLE_AUDIO))
    {
        if (ctx->audio)
        {
            bool was_muted = sdl2_audio_is_muted(ctx->audio);
            sdl2_audio_set_muted(ctx->audio, !was_muted);
            message_system_set(ctx->message, was_muted ? "- Audio ON -" : "- Audio OFF -", 1,
                               frame);
        }
        else
        {
            message_system_set(ctx->message, "- Audio unavailable -", 1, frame);
        }
    }

    /* H: show highscores — original/main.c:608-636.
     * H (shift) = PERSONAL, h = GLOBAL. Attract-only. */
    if (is_attract && sdl2_input_just_pressed(ctx->input, SDL2I_SCORES))
    {
        bool shift = sdl2_input_shift_held(ctx->input);
        ctx->highscore_request_type = shift ? HIGHSCORE_TYPE_PERSONAL : HIGHSCORE_TYPE_GLOBAL;
        sdl2_loop_set_speed(ctx->loop, SDL2L_DEFAULT_SPEED);
        sdl2_state_transition(ctx->state, SDL2ST_HIGHSCORE);
        if (ctx->audio)
            sdl2_audio_play_at_percent(ctx->audio, "toggle", 50);
    }

    /* +/-: volume up/down — original/main.c:822 */
    if (ctx->audio)
    {
        if (sdl2_input_just_pressed(ctx->input, SDL2I_VOLUME_UP))
        {
            int vol = sdl2_audio_volume_up(ctx->audio);
            char str[32];
            snprintf(str, sizeof(str), "Maximum volume: %d%%", vol);
            message_system_set(ctx->message, str, 1, frame);
        }
        else if (sdl2_input_just_pressed(ctx->input, SDL2I_VOLUME_DOWN))
        {
            int vol = sdl2_audio_volume_down(ctx->audio);
            char str[32];
            snprintf(str, sizeof(str), "Maximum volume: %d%%", vol);
            message_system_set(ctx->message, str, 1, frame);
        }
    }

    /* I: fullscreen toggle — original/main.c:853 (XIconifyWindow).
     * Modernized as fullscreen toggle since SDL2 window management
     * differs from X11 iconify semantics. See ADR in docs/DESIGN.md. */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_ICONIFY))
        sdl2_renderer_toggle_fullscreen(ctx->renderer);

    /* G: toggle keyboard/mouse control — original/main.c:377-394 */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_TOGGLE_CONTROL))
    {
        ctx->config.use_keys = !ctx->config.use_keys;
        message_system_set(ctx->message, ctx->config.use_keys ? "Control: Keys" : "Control: Mouse",
                           1, frame);
        if (ctx->audio)
            sdl2_audio_play_at_percent(ctx->audio, "toggle", 50);
    }

    /* --- Gameplay keys (GAME mode only) --- */
    if (mode == SDL2ST_GAME)
    {
        /* K: shoot/activate — original/main.c:490-494 */
        if (sdl2_input_just_pressed(ctx->input, SDL2I_SHOOT))
        {
            ball_system_env_t benv = game_callbacks_ball_env(ctx);
            if (ball_system_activate_waiting(ctx->ball, &benv) == -1)
            {
                gun_system_env_t genv = game_callbacks_gun_env(ctx);
                gun_system_shoot(ctx->gun, &genv);
            }
        }

        /* Mouse click: same dual-use as K — original/main.c:357-366 */
        if (sdl2_input_mouse_just_pressed(ctx->input, SDL_BUTTON_LEFT) ||
            sdl2_input_mouse_just_pressed(ctx->input, SDL_BUTTON_MIDDLE) ||
            sdl2_input_mouse_just_pressed(ctx->input, SDL_BUTTON_RIGHT))
        {
            ball_system_env_t benv = game_callbacks_ball_env(ctx);
            if (ball_system_activate_waiting(ctx->ball, &benv) == -1)
            {
                gun_system_env_t genv = game_callbacks_gun_env(ctx);
                gun_system_shoot(ctx->gun, &genv);
            }
        }

        /* D: kill an active ball — original/main.c:475-483 */
        if (sdl2_input_just_pressed(ctx->input, SDL2I_KILL_BALL))
        {
            int idx = ball_system_get_active_index(ctx->ball);
            if (idx >= 0)
            {
                ball_system_env_t benv = game_callbacks_ball_env(ctx);
                ball_system_change_mode(ctx->ball, &benv, idx, BALL_POP);
            }
        }

        /* T: tilt the board — original/main.c:451-473 */
        if (sdl2_input_just_pressed(ctx->input, SDL2I_TILT))
        {
            int idx = ball_system_get_active_index(ctx->ball);
            if (idx >= 0)
            {
                if (ctx->user_tilts < GAME_MAX_TILTS)
                {
                    ball_system_env_t benv = game_callbacks_ball_env(ctx);
                    ball_system_do_tilt(ctx->ball, &benv, idx);
                    ctx->user_tilts++;
                    int left = GAME_MAX_TILTS - ctx->user_tilts;
                    char msg[64];
                    snprintf(msg, sizeof(msg), "You have %d %s left!", left,
                             left == 1 ? "tilt" : "tilts");
                    message_system_set(ctx->message, msg, 1, frame);
                }
                else
                {
                    message_system_set(ctx->message, "Maximum tilts reached!", 1, frame);
                }
            }
        }

        /* Z saves, X loads */
        if (sdl2_input_just_pressed(ctx->input, SDL2I_SAVE))
            (void)savegame_system_save(ctx);
        if (sdl2_input_just_pressed(ctx->input, SDL2I_LOAD))
            (void)savegame_system_load(ctx);

        /* Escape — original/main.c:506-508.
         * If play-testing from editor: end the test via the same path
         * as P (editor_system_key_input -> on_playtest_end), restoring
         * the pre-test board and clearing play_test_active — matches
         * FinishPlayTest (original/editor.c:994-1001).
         * Otherwise: "Abort current game? [y/n]" confirmation.
         *
         * Reads ctx->play_test_active rather than re-deriving the same
         * fact from sdl2_state_previous() == SDL2ST_EDIT -- the latter
         * is a single-slot value silently overwritten by the next
         * transition and was flagged as the fragile precedent this flag
         * retires (docs/specs/2026-07-12-playtest-fidelity.md S3.1). */
        if (sdl2_input_just_pressed(ctx->input, SDL2I_ABORT))
        {
            if (ctx->play_test_active)
            {
                editor_system_key_input(ctx->editor, EDITOR_KEY_PLAYTEST);
                /* Prevents the same-frame mode_edit_update from re-processing
                 * the key (xboing-1ir). */
                sdl2_input_consume(ctx->input, SDL2I_ABORT);
                /* The transition above already moved the state machine to
                 * SDL2ST_EDIT, so the local `mode` snapshot is stale for the
                 * rest of this frame. Stop here rather than let later
                 * GAME-gated handlers below run against the just-restored
                 * editor board. */
                return;
            }
            else if (sdl2_state_push_dialogue(ctx->state) == SDL2ST_OK)
            {
                dialogue_system_open(ctx->dialogue, "Abort current game? [y/n]", DIALOGUE_ICON_TEXT,
                                     DIALOGUE_VALIDATION_YES_NO);
                game_modes_set_abort_pending();
            }
        }
    }

    /* E: enter editor — original only handles E in handleGameKeys and
     * handleIntroKeys, not during pause/presents/dialogue. */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_ENTER_EDITOR) &&
        (mode == SDL2ST_GAME || is_attract))
        sdl2_state_transition(ctx->state, SDL2ST_EDIT);

    /* W: set starting level — original/main.c:671-673, level.c:245-282.
     * Attract-only.  Shows range message then opens numeric dialogue. */
    if (is_attract && sdl2_input_just_pressed(ctx->input, SDL2I_SET_LEVEL) &&
        mode != SDL2ST_DIALOGUE)
    {
        char range_msg[64];
        snprintf(range_msg, sizeof(range_msg), "Level range is [1-%d]", LEVEL_MAX_NUM);
        /* auto_clear=1: transient hint, expires after MESSAGE_CLEAR_DELAY.
         * Without this, a cancelled dialog leaves the range hint stuck
         * in the message bar forever (mode_dialogue_exit's level_pending
         * cancel branch doesn't touch the bar). */
        message_system_set(ctx->message, range_msg, 1, frame);

        if (sdl2_state_push_dialogue(ctx->state) == SDL2ST_OK)
        {
            dialogue_system_open(ctx->dialogue, "Input game starting level number.",
                                 DIALOGUE_ICON_TEXT, DIALOGUE_VALIDATION_NUMERIC);
            game_modes_set_level_pending();
        }
    }

    /* Q: quit with confirmation — original/main.c:864-868.
     * "Exit XBoing you wimp? [y/n]" YesNoDialogue.
     * Blocked in EDIT (editor has own Q handler), DIALOGUE (prevent
     * setting quit_pending during unrelated dialogue), and during
     * play-test — the original EDIT_TEST switch swallows Q entirely
     * (original/editor.c:992-1030 handles only p/paddle/shoot and
     * returns before reaching the Q exit-dialogue case). */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_QUIT) && mode != SDL2ST_EDIT &&
        mode != SDL2ST_DIALOGUE && !ctx->play_test_active)
    {
        if (sdl2_state_push_dialogue(ctx->state) == SDL2ST_OK)
        {
            dialogue_system_open(ctx->dialogue, "Exit XBoing you wimp? [y/n]", DIALOGUE_ICON_TEXT,
                                 DIALOGUE_VALIDATION_YES_NO);
            game_modes_set_quit_pending();
        }
    }

    /* C: cycle attract screens — original/main.c:554-605.
     * Uses game_callbacks_attract_next() as single source of truth for
     * cycle order.  Original calls SetGameSpeed(FAST_SPEED) before each. */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_CYCLE))
    {
        sdl2_state_mode_t next = game_callbacks_attract_next(mode);
        if (next != SDL2ST_NONE)
        {
            sdl2_loop_set_speed(ctx->loop, SDL2L_DEFAULT_SPEED);
            sdl2_state_transition(ctx->state, next);
        }
    }

    /* Space on the attract screens.  Handled here once per visual frame, NOT
     * in the per-tick mode_*_update handlers: the fixed-timestep loop runs
     * several ticks per frame (a large catch-up burst on the first frame), so
     * a per-tick edge handler let a single press cascade through several
     * screens straight into the game.  `mode` is captured once at frame start,
     * so exactly one transition fires per press.  Mirrors the C-cycle above.
     *
     * Title -> instructions (not the game); the other attract screens ->
     * game; highscore -> new game, or intro if shown after a game over.
     *
     * PRESENTS and BONUS are intentionally absent: their Space merely
     * fast-forwards that one screen (presents_system_skip / bonus_system_skip),
     * which is idempotent and cannot cascade, so it stays per-tick in their
     * mode_*_update handlers. */
    if (sdl2_input_just_pressed(ctx->input, SDL2I_START))
    {
        if (mode == SDL2ST_INTRO)
        {
            sdl2_loop_set_speed(ctx->loop, SDL2L_DEFAULT_SPEED);
            sdl2_state_transition(ctx->state, SDL2ST_INSTRUCT);
        }
        else if (mode == SDL2ST_INSTRUCT || mode == SDL2ST_DEMO || mode == SDL2ST_PREVIEW ||
                 mode == SDL2ST_KEYS || mode == SDL2ST_KEYSEDIT)
        {
            sdl2_state_transition(ctx->state, SDL2ST_GAME);
        }
        else if (mode == SDL2ST_HIGHSCORE)
        {
            if (ctx->game_active)
            {
                /* Game-over Highscore: return to the title.  game_active is
                 * cleared by mode_intro_enter (the single clear site all
                 * attract exits from Highscore funnel through) — see ADR-055. */
                sdl2_state_transition(ctx->state, SDL2ST_INTRO);
            }
            else
            {
                sdl2_state_transition(ctx->state, SDL2ST_GAME);
            }
        }
    }
}
