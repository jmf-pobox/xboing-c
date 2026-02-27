/*
 * game_init.c -- Create and destroy all modules for the SDL2 game.
 *
 * game_create() builds the full game_ctx_t in dependency order:
 *   1. CLI + config + paths (no SDL2 needed)
 *   2. SDL2 platform modules (renderer → texture → font → audio → input → cursor)
 *   3. Pure C state/loop modules
 *   4. Game systems (block → paddle → ball → gun → score → level → etc.)
 *   5. UI sequencers (presents, intro, demo, keys, dialogue, highscore)
 *
 * game_destroy() tears down in reverse order.
 *
 * All module callbacks are initially stubbed (NULL or no-op).  Integration
 * modules (game_callbacks.c, game_modes.c) wire real callbacks later.
 */

#include "game_init.h"
#include "game_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "ball_system.h"
#include "block_system.h"
#include "bonus_system.h"
#include "config_io.h"
#include "demo_system.h"
#include "dialogue_system.h"
#include "editor_system.h"
#include "eyedude_system.h"
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
#include "sdl2_cli.h"
#include "sdl2_cursor.h"
#include "sdl2_font.h"
#include "sdl2_input.h"
#include "sdl2_loop.h"
#include "sdl2_renderer.h"
#include "sdl2_state.h"
#include "sdl2_texture.h"
#include "sfx_system.h"
#include "special_system.h"

/* =========================================================================
 * Play area constants (from stage.h, duplicated to avoid legacy headers)
 * ========================================================================= */

#define GAME_PLAY_WIDTH 495
#define GAME_PLAY_HEIGHT 580
#define GAME_MAIN_WIDTH 70
#define GAME_COL_WIDTH (GAME_PLAY_WIDTH / 9)    /* 55 */
#define GAME_ROW_HEIGHT (GAME_PLAY_HEIGHT / 18) /* 32 */

/* =========================================================================
 * Forward declarations for stub callbacks (game_main.c provides real ones)
 * ========================================================================= */

/* These will be replaced when game_modes.c / game_callbacks.c are wired. */
static void stub_tick(void *user_data);
static void stub_render(double alpha, void *user_data);

/* Level → block system bridge callback */
static void on_level_add_block(int row, int col, int block_type, int counter_slide, void *ud);

/* =========================================================================
 * game_create
 * ========================================================================= */

game_ctx_t *game_create(int argc, char *argv[])
{
    game_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
    {
        fprintf(stderr, "game_create: allocation failed\n");
        return NULL;
    }

    /* ---- Phase 1: CLI + config + paths ---------------------------------- */

    /* Parse CLI */
    sdl2_cli_config_t cli = sdl2_cli_config_defaults();
    const char *bad_option = NULL;
    sdl2_cli_status_t cli_status = sdl2_cli_parse(argc, argv, &cli, &bad_option);

    if (cli_status == SDL2C_EXIT_HELP || cli_status == SDL2C_EXIT_VERSION)
    {
        free(ctx);
        return NULL; /* Caller should exit 0 */
    }
    if (cli_status == SDL2C_EXIT_SETUP || cli_status == SDL2C_EXIT_SCORES)
    {
        free(ctx);
        return NULL; /* Caller should print info and exit 0 */
    }
    if (cli_status != SDL2C_OK)
    {
        fprintf(stderr, "Error: %s", sdl2_cli_status_string(cli_status));
        if (bad_option)
            fprintf(stderr, ": %s", bad_option);
        fprintf(stderr, "\n");
        free(ctx);
        return NULL;
    }

    /* Initialize paths */
    if (paths_init(&ctx->paths) != PATHS_OK)
    {
        fprintf(stderr, "game_create: failed to initialize paths\n");
        free(ctx);
        return NULL;
    }

    /* Load config file (or defaults if not found) */
    char config_path[PATHS_MAX_PATH];
    paths_status_t ps = paths_user_data_dir(&ctx->paths, config_path, sizeof(config_path));
    if (ps == PATHS_OK)
    {
        size_t len = strlen(config_path);
        snprintf(config_path + len, sizeof(config_path) - len, "/config.toml");
        config_io_read(config_path, &ctx->config);
    }
    else
    {
        config_io_init(&ctx->config);
    }

    /* CLI overrides config */
    ctx->config.speed = cli.speed;
    ctx->config.use_keys = cli.use_keys;
    ctx->config.sfx = cli.sfx;
    ctx->config.sound = cli.sound;
    if (cli.max_volume > 0)
        ctx->config.max_volume = cli.max_volume;
    if (cli.start_level > 1)
        ctx->config.start_level = cli.start_level;
    if (cli.nickname[0] != '\0')
        memcpy(ctx->config.nickname, cli.nickname, sizeof(ctx->config.nickname));

    /* Set initial game state */
    ctx->level_number = ctx->config.start_level;
    ctx->start_level = ctx->config.start_level;
    ctx->lives_left = 3;
    ctx->debug_mode = cli.debug;

    /* Load high score tables */
    highscore_io_init_table(&ctx->hs_global);
    highscore_io_init_table(&ctx->hs_personal);

    char score_path[PATHS_MAX_PATH];
    if (paths_score_file_global(&ctx->paths, score_path, sizeof(score_path)) == PATHS_OK)
        highscore_io_read(score_path, &ctx->hs_global);
    if (paths_score_file_personal(&ctx->paths, score_path, sizeof(score_path)) == PATHS_OK)
        highscore_io_read(score_path, &ctx->hs_personal);

    /* ---- Phase 2: SDL2 platform modules --------------------------------- */

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        free(ctx);
        return NULL;
    }

    /* Renderer */
    sdl2_renderer_config_t rcfg = sdl2_renderer_config_defaults();
    ctx->renderer = sdl2_renderer_create(&rcfg);
    if (!ctx->renderer)
    {
        fprintf(stderr, "game_create: renderer creation failed\n");
        goto fail;
    }

    /* Texture cache */
    {
        sdl2_texture_config_t tcfg = sdl2_texture_config_defaults();
        tcfg.renderer = sdl2_renderer_get(ctx->renderer);
        sdl2_texture_status_t ts;
        ctx->texture = sdl2_texture_create(&tcfg, &ts);
        if (!ctx->texture)
        {
            fprintf(stderr, "game_create: texture cache creation failed: %s\n",
                    sdl2_texture_status_string(ts));
            goto fail;
        }
    }

    /* Font */
    {
        sdl2_font_config_t fcfg = sdl2_font_config_defaults();
        fcfg.renderer = sdl2_renderer_get(ctx->renderer);
        sdl2_font_status_t fs;
        ctx->font = sdl2_font_create(&fcfg, &fs);
        if (!ctx->font)
        {
            fprintf(stderr, "game_create: font creation failed: %s\n", sdl2_font_status_string(fs));
            goto fail;
        }
    }

    /* Audio (optional — game works without sound) */
    if (ctx->config.sound)
    {
        sdl2_audio_config_t acfg = sdl2_audio_config_defaults();
        sdl2_audio_status_t as;
        ctx->audio = sdl2_audio_create(&acfg, &as);
        if (!ctx->audio)
        {
            fprintf(stderr, "Warning: audio creation failed: %s (continuing without sound)\n",
                    sdl2_audio_status_string(as));
        }
        else if (ctx->config.max_volume > 0)
        {
            sdl2_audio_set_volume_percent(ctx->audio, ctx->config.max_volume);
        }
    }

    /* Input */
    {
        sdl2_input_status_t is;
        ctx->input = sdl2_input_create(&is);
        if (!ctx->input)
        {
            fprintf(stderr, "game_create: input creation failed\n");
            goto fail;
        }
    }

    /* Cursor */
    {
        sdl2_cursor_status_t cs;
        ctx->cursor = sdl2_cursor_create(&cs);
        if (!ctx->cursor)
        {
            fprintf(stderr, "game_create: cursor creation failed\n");
            goto fail;
        }
    }

    /* ---- Phase 3: State machine + game loop ----------------------------- */

    {
        sdl2_state_status_t ss;
        ctx->state = sdl2_state_create(ctx, &ss);
        if (!ctx->state)
        {
            fprintf(stderr, "game_create: state machine creation failed\n");
            goto fail;
        }
    }

    {
        sdl2_loop_status_t ls;
        ctx->loop = sdl2_loop_create(stub_tick, stub_render, ctx, &ls);
        if (!ctx->loop)
        {
            fprintf(stderr, "game_create: game loop creation failed\n");
            goto fail;
        }
        sdl2_loop_set_speed(ctx->loop, ctx->config.speed);
    }

    /* ---- Phase 4: Game systems ------------------------------------------ */

    /* Block system */
    {
        block_system_status_t bs;
        ctx->block = block_system_create(GAME_COL_WIDTH, GAME_ROW_HEIGHT, &bs);
        if (!ctx->block)
        {
            fprintf(stderr, "game_create: block system creation failed\n");
            goto fail;
        }
    }

    /* Paddle system */
    {
        paddle_system_status_t ps2;
        ctx->paddle =
            paddle_system_create(GAME_PLAY_WIDTH, GAME_PLAY_HEIGHT, GAME_MAIN_WIDTH, &ps2);
        if (!ctx->paddle)
        {
            fprintf(stderr, "game_create: paddle system creation failed\n");
            goto fail;
        }
    }

    /* Ball system (stub callbacks — wired by game_callbacks.c) */
    {
        ball_system_callbacks_t bcb = {0};
        ball_system_status_t bs;
        ctx->ball = ball_system_create(&bcb, ctx, &bs);
        if (!ctx->ball)
        {
            fprintf(stderr, "game_create: ball system creation failed\n");
            goto fail;
        }
    }

    /* Gun system (stub callbacks) */
    {
        gun_system_callbacks_t gcb = {0};
        gun_system_status_t gs;
        ctx->gun = gun_system_create(GAME_PLAY_HEIGHT, &gcb, ctx, &gs);
        if (!ctx->gun)
        {
            fprintf(stderr, "game_create: gun system creation failed\n");
            goto fail;
        }
    }

    /* Score system (stub callbacks) */
    {
        score_system_callbacks_t scb = {0};
        score_system_status_t ss;
        ctx->score = score_system_create(&scb, ctx, &ss);
        if (!ctx->score)
        {
            fprintf(stderr, "game_create: score system creation failed\n");
            goto fail;
        }
    }

    /* Level system (wired to block system) */
    {
        level_system_callbacks_t lcb = {.on_add_block = on_level_add_block};
        level_system_status_t ls;
        ctx->level = level_system_create(&lcb, ctx, &ls);
        if (!ctx->level)
        {
            fprintf(stderr, "game_create: level system creation failed\n");
            goto fail;
        }
    }

    /* Special system (stub callbacks) */
    {
        special_system_callbacks_t scb = {0};
        ctx->special = special_system_create(&scb, ctx);
        if (!ctx->special)
        {
            fprintf(stderr, "game_create: special system creation failed\n");
            goto fail;
        }
    }

    /* Bonus system (stub callbacks) */
    {
        bonus_system_callbacks_t bcb = {0};
        ctx->bonus = bonus_system_create(&bcb, ctx);
        if (!ctx->bonus)
        {
            fprintf(stderr, "game_create: bonus system creation failed\n");
            goto fail;
        }
    }

    /* SFX system (stub callbacks) */
    {
        sfx_system_callbacks_t scb = {0};
        ctx->sfx = sfx_system_create(&scb, ctx, NULL);
        if (!ctx->sfx)
        {
            fprintf(stderr, "game_create: sfx system creation failed\n");
            goto fail;
        }
    }

    /* EyeDude system (stub callbacks) */
    {
        eyedude_system_callbacks_t ecb = {0};
        ctx->eyedude = eyedude_system_create(&ecb, ctx, NULL);
        if (!ctx->eyedude)
        {
            fprintf(stderr, "game_create: eyedude system creation failed\n");
            goto fail;
        }
    }

    /* Message system */
    ctx->message = message_system_create();
    if (!ctx->message)
    {
        fprintf(stderr, "game_create: message system creation failed\n");
        goto fail;
    }

    /* Editor system (stub callbacks) */
    {
        editor_system_callbacks_t ecb = {0};
        char levels_dir[PATHS_MAX_PATH] = "levels";
        paths_levels_dir(&ctx->paths, levels_dir, sizeof(levels_dir));
        ctx->editor = editor_system_create(&ecb, ctx, levels_dir, !ctx->config.sound);
        if (!ctx->editor)
        {
            fprintf(stderr, "game_create: editor system creation failed\n");
            goto fail;
        }
    }

    /* ---- Phase 5: UI sequencers ----------------------------------------- */

    /* Presents */
    {
        presents_system_callbacks_t pcb = {0};
        ctx->presents = presents_system_create(&pcb, ctx);
        if (!ctx->presents)
        {
            fprintf(stderr, "game_create: presents system creation failed\n");
            goto fail;
        }
    }

    /* Intro */
    {
        intro_system_callbacks_t icb = {0};
        ctx->intro = intro_system_create(&icb, ctx, NULL);
        if (!ctx->intro)
        {
            fprintf(stderr, "game_create: intro system creation failed\n");
            goto fail;
        }
    }

    /* Demo */
    {
        demo_system_callbacks_t dcb = {0};
        ctx->demo = demo_system_create(&dcb, ctx, NULL);
        if (!ctx->demo)
        {
            fprintf(stderr, "game_create: demo system creation failed\n");
            goto fail;
        }
    }

    /* Keys */
    {
        keys_system_callbacks_t kcb = {0};
        ctx->keys = keys_system_create(&kcb, ctx, NULL);
        if (!ctx->keys)
        {
            fprintf(stderr, "game_create: keys system creation failed\n");
            goto fail;
        }
    }

    /* Dialogue */
    ctx->dialogue = dialogue_system_create();
    if (!ctx->dialogue)
    {
        fprintf(stderr, "game_create: dialogue system creation failed\n");
        goto fail;
    }

    /* High score display */
    {
        highscore_system_callbacks_t hcb = {0};
        ctx->highscore_display = highscore_system_create(&hcb, ctx);
        if (!ctx->highscore_display)
        {
            fprintf(stderr, "game_create: highscore display creation failed\n");
            goto fail;
        }
    }

    /* ---- Phase 6: Load initial level ------------------------------------ */
    {
        int file_num = level_system_wrap_number(ctx->level_number);
        char filename[32];
        snprintf(filename, sizeof(filename), "level%02d.data", file_num);

        char level_path[PATHS_MAX_PATH];
        if (paths_level_file(&ctx->paths, filename, level_path, sizeof(level_path)) == PATHS_OK)
        {
            block_system_clear_all(ctx->block);
            level_system_advance_background(ctx->level);
            level_system_load_file(ctx->level, level_path);
        }
        else
        {
            fprintf(stderr, "Warning: could not find level file: %s\n", filename);
        }
    }

    return ctx;

fail:
    game_destroy(ctx);
    return NULL;
}

/* =========================================================================
 * game_destroy
 * ========================================================================= */

void game_destroy(game_ctx_t *ctx)
{
    if (!ctx)
        return;

    /* Phase 5: UI sequencers (reverse order) */
    highscore_system_destroy(ctx->highscore_display);
    dialogue_system_destroy(ctx->dialogue);
    keys_system_destroy(ctx->keys);
    demo_system_destroy(ctx->demo);
    intro_system_destroy(ctx->intro);
    presents_system_destroy(ctx->presents);

    /* Phase 4: Game systems (reverse order) */
    editor_system_destroy(ctx->editor);
    message_system_destroy(ctx->message);
    eyedude_system_destroy(ctx->eyedude);
    sfx_system_destroy(ctx->sfx);
    bonus_system_destroy(ctx->bonus);
    special_system_destroy(ctx->special);
    level_system_destroy(ctx->level);
    score_system_destroy(ctx->score);
    gun_system_destroy(ctx->gun);
    ball_system_destroy(ctx->ball);
    paddle_system_destroy(ctx->paddle);
    block_system_destroy(ctx->block);

    /* Phase 3: State + loop */
    sdl2_loop_destroy(ctx->loop);
    sdl2_state_destroy(ctx->state);

    /* Phase 2: SDL2 platform (reverse order) */
    sdl2_cursor_destroy(ctx->cursor);
    sdl2_input_destroy(ctx->input);
    sdl2_audio_destroy(ctx->audio);
    sdl2_font_destroy(ctx->font);
    sdl2_texture_destroy(ctx->texture);
    sdl2_renderer_destroy(ctx->renderer);

    SDL_Quit();

    free(ctx);
}

/* =========================================================================
 * Stub callbacks (replaced by game_modes.c / game_callbacks.c)
 * ========================================================================= */

static void stub_tick(void *user_data)
{
    game_ctx_t *ctx = user_data;
    sdl2_state_update(ctx->state);
}

static void stub_render(double alpha, void *user_data)
{
    (void)alpha;
    game_ctx_t *ctx = user_data;
    game_render_frame(ctx);
}

/* =========================================================================
 * Level → block system bridge
 * ========================================================================= */

static void on_level_add_block(int row, int col, int block_type, int counter_slide, void *ud)
{
    game_ctx_t *ctx = ud;
    /* frame=0 for initial load — animation timing is irrelevant */
    block_system_add(ctx->block, row, col, block_type, counter_slide, 0);
}
