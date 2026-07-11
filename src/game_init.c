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
#include "game_callbacks.h"
#include "game_modes.h"
#include "game_render.h"
#include "game_rules.h"

#include <dirent.h> /* opendir/closedir for asset-dir readability check */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
#include "sys_priv.h"
#include "xboing_paths.h"
#include "xboing_version.h"

/* =========================================================================
 * Play area constants (from stage.h, duplicated to avoid legacy headers)
 * ========================================================================= */

#define GAME_MAIN_WIDTH 70

/* =========================================================================
 * Forward declarations for stub callbacks (game_main.c provides real ones)
 * ========================================================================= */

/* These will be replaced when game_modes.c / game_callbacks.c are wired. */
static void stub_tick(void *user_data);
static void stub_render(double alpha, void *user_data);

/* Level → block system bridge callback */
static void on_level_add_block(int row, int col, int block_type, int counter_slide, void *ud);

/* Informational-flag handlers (xboing -help / -version / -setup / -scores). */
static void print_usage(FILE *out);
static void print_setup_info(const paths_config_t *cfg);
static void print_scores(const paths_config_t *cfg);

/* Return non-zero if path is a directory we can list.  opendir() succeeds
 * iff the path exists, is a directory, and is readable + executable for
 * us — which is exactly the condition the subsequent directory scan
 * needs.  stat()+S_ISDIR alone wouldn't catch a perms-locked dir; the
 * later scan would still fail, just with a worse error message. */
static int asset_dir_exists(const char *path)
{
    DIR *d = opendir(path);
    if (d == NULL)
        return 0;
    closedir(d);
    return 1;
}

/* =========================================================================
 * Informational flag output
 * ========================================================================= */

static void print_usage(FILE *out)
{
    fprintf(out, "Usage: xboing [OPTIONS]\n"
                 "\n"
                 "Game options:\n"
                 "  -speed <1-9>        Game speed (1=slowest, 9=fastest, default 5)\n"
                 "  -startlevel <1-80>  Start at the given level (default 1)\n"
                 "  -keys               Use keyboard control (default: mouse)\n"
                 "  -nickname <name>    Set high-score nickname\n"
                 "  -debug              Enable debug mode\n"
                 "  -grab               Grab pointer to window\n"
                 "  -load               On startup, autoload the saved game (skips\n"
                 "                      attract cycle); used by visual-capture scripts\n"
                 "  -nosfx              Disable visual special effects (e.g. screen "
                 "shake)\n"
                 "\n"
                 "Audio options:\n"
                 "  -sound              Enable sound (default)\n"
                 "  -nosound            Disable all audio\n"
                 "  -maxvol <0-100>     Maximum volume\n"
                 "\n"
                 "Information (these print and exit):\n"
                 "  -help, -usage       Show this help\n"
                 "  -version            Show version\n"
                 "  -setup              Show resolved configuration paths\n"
                 "  -scores             Show high scores (personal + global)\n");
}

static void print_setup_info(const paths_config_t *cfg)
{
    char buf[PATHS_MAX_PATH];
    printf("xboing %s configuration:\n\n", XBOING_VERSION);
    printf("  HOME              = %s\n", cfg->home);
    printf("  XDG_DATA_HOME     = %s\n", cfg->xdg_data_home);
    printf("  XDG_CONFIG_HOME   = %s\n", cfg->xdg_config_home);
    if (cfg->xboing_levels_dir[0])
        printf("  XBOING_LEVELS_DIR = %s\n", cfg->xboing_levels_dir);
    if (cfg->xboing_sound_dir[0])
        printf("  XBOING_SOUND_DIR  = %s\n", cfg->xboing_sound_dir);
    if (cfg->xboing_score_file[0])
        printf("  XBOING_SCORE_FILE = %s\n", cfg->xboing_score_file);
    printf("\nResolved paths:\n");
    if (paths_levels_dir_readable(cfg, buf, sizeof(buf)) == PATHS_OK)
        printf("  Levels dir            = %s\n", buf);
    if (paths_levels_dir_writable(cfg, buf, sizeof(buf)) == PATHS_OK)
        printf("  Levels dir (editor)   = %s\n", buf);
    if (paths_sounds_dir_readable(cfg, buf, sizeof(buf)) == PATHS_OK)
        printf("  Sounds dir            = %s\n", buf);
    if (paths_score_file_global(cfg, buf, sizeof(buf)) == PATHS_OK)
        printf("  Score file (global)   = %s\n", buf);
    if (paths_score_file_personal(cfg, buf, sizeof(buf)) == PATHS_OK)
        printf("  Score file (personal) = %s\n", buf);
    if (paths_user_data_dir(cfg, buf, sizeof(buf)) == PATHS_OK)
        printf("  User data dir         = %s\n", buf);
}

/* Read and print one high-score table.  Returns false when the file could
 * not be opened — missing OR unreadable (highscore_io_read returns
 * HIGHSCORE_IO_ERR_OPEN for any fopen failure) — so the caller can word
 * that case; a read/parse error is reported here and returns true. */
static bool print_one_score_table(const char *label, const char *path)
{
    highscore_table_t table;
    highscore_io_init_table(&table);
    highscore_io_result_t r = highscore_io_read(path, &table);
    if (r == HIGHSCORE_IO_ERR_OPEN)
        return false;
    if (r != HIGHSCORE_IO_OK)
    {
        fprintf(stderr, "xboing -scores: failed to read %s (code %d)\n", path, (int)r);
        return true;
    }

    printf("%s (%s):\n\n", label, path);
    if (table.master_name[0])
        printf("  Master: %s — \"%s\"\n\n", table.master_name, table.master_text);
    int shown = 0;
    for (int i = 0; i < HIGHSCORE_NUM_ENTRIES; i++)
    {
        if (table.entries[i].score == 0)
            continue;
        printf("  %2d. %-20s %10lu  level %2lu\n", i + 1, table.entries[i].name,
               table.entries[i].score, table.entries[i].level);
        shown++;
    }
    if (shown == 0)
        printf("  (no scored entries)\n");
    printf("\n");
    return true;
}

static void print_scores(const paths_config_t *cfg)
{
    char path[PATHS_MAX_PATH];

    /* Personal table exists on every platform. */
    if (paths_score_file_personal(cfg, path, sizeof(path)) != PATHS_OK)
        fprintf(stderr, "xboing -scores: cannot resolve the personal score file path\n");
    else if (!print_one_score_table("Personal high scores", path))
        printf("No personal scores to show: could not open %s "
               "(none recorded yet, or it is unreadable).\n\n",
               path);

    /* The shared/global ("roll of honour") table exists only on the setgid
     * Debian install or when an explicit XBOING_SCORE_FILE is configured.
     * On unprivileged installs (Homebrew, dev builds) there is no shared
     * board, so do not reference the FHS /var/games path that can never
     * apply there. */
    if (sys_priv_global_board_active(cfg->xboing_score_file))
    {
        if (paths_score_file_global(cfg, path, sizeof(path)) != PATHS_OK)
            fprintf(stderr, "xboing -scores: cannot resolve the global score file path\n");
        else if (!print_one_score_table("Global high scores", path))
            printf("No global scores to show: could not open %s "
                   "(none recorded yet, or it is unreadable).\n",
                   path);
    }
}

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

    /* Handle informational flags that don't need paths first. */
    if (cli_status == SDL2C_EXIT_HELP)
    {
        print_usage(stdout);
        free(ctx);
        return NULL;
    }
    if (cli_status == SDL2C_EXIT_VERSION)
    {
        printf("xboing %s (SDL2 modernization)\n", XBOING_VERSION);
        free(ctx);
        return NULL;
    }
    /* Only abort on real errors here — SETUP and SCORES need paths first. */
    if (cli_status != SDL2C_OK && cli_status != SDL2C_EXIT_SETUP && cli_status != SDL2C_EXIT_SCORES)
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

    /* Informational flags that depend on resolved paths. */
    if (cli_status == SDL2C_EXIT_SETUP)
    {
        print_setup_info(&ctx->paths);
        free(ctx);
        return NULL;
    }
    if (cli_status == SDL2C_EXIT_SCORES)
    {
        print_scores(&ctx->paths);
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
    ctx->bonus_count = 0;
    ctx->debug_mode = cli.debug;
    ctx->vc_mode = cli.visual_capture_mode;
    ctx->vc_interval = cli.visual_capture_interval;
    ctx->autoload = cli.autoload;

    /* Load high score tables */
    highscore_io_init_table(&ctx->hs_global);
    highscore_io_init_table(&ctx->hs_personal);

    char score_path[PATHS_MAX_PATH];
    if (paths_score_file_global(&ctx->paths, score_path, sizeof(score_path)) == PATHS_OK)
        highscore_io_read(score_path, &ctx->hs_global);
    if (paths_score_file_personal(&ctx->paths, score_path, sizeof(score_path)) == PATHS_OK)
        highscore_io_read(score_path, &ctx->hs_personal);
    /* Original defaults to GLOBAL — original/highscore.c:136
     * (static int scoreType = GLOBAL).  master_text only lives in the
     * global table, so this also ensures new boing-master wisdom is
     * visible on the post-game-over screen. */
    ctx->highscore_request_type = HIGHSCORE_TYPE_GLOBAL;

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

    /* -grab: confine the mouse pointer to the window (original/main.c:248
     * grabbed via XGrabPointer with confine_to=window). */
    sdl2_renderer_set_mouse_grab(ctx->renderer, cli.grab);

    /* Texture cache.  Resolution order (matches paths.c's level/sound
     * file lookup, freedesktop XDG Base Directory spec):
     *   1. $XDG_DATA_DIRS/xboing/images  (handles --prefix=/usr,
     *      /usr/local, etc. transparently — same mechanism Debian
     *      games and GNOME apps use)
     *   2. XBOING_INSTALLED_IMAGES_DIR  (compile-time fallback for
     *      unusual installs not in $XDG_DATA_DIRS)
     *   3. cwd-relative "assets/images"  (dev mode default in
     *      sdl2_texture_config_defaults) */
    char tex_dir[PATHS_MAX_PATH];
    {
        sdl2_texture_config_t tcfg = sdl2_texture_config_defaults();
        tcfg.renderer = sdl2_renderer_get(ctx->renderer);
        if (paths_install_data_dir(&ctx->paths, "images", tex_dir, sizeof(tex_dir)) == PATHS_OK)
            tcfg.base_dir = tex_dir;
        else if (asset_dir_exists(XBOING_INSTALLED_IMAGES_DIR))
            tcfg.base_dir = XBOING_INSTALLED_IMAGES_DIR;
        sdl2_texture_status_t ts;
        ctx->texture = sdl2_texture_create(&tcfg, &ts);
        if (!ctx->texture)
        {
            fprintf(stderr, "game_create: texture cache creation failed: %s\n",
                    sdl2_texture_status_string(ts));
            goto fail;
        }
    }

    /* Font.  Same XDG-first resolution as texture cache above. */
    char font_dir[PATHS_MAX_PATH];
    {
        sdl2_font_config_t fcfg = sdl2_font_config_defaults();
        fcfg.renderer = sdl2_renderer_get(ctx->renderer);
        if (paths_install_data_dir(&ctx->paths, "fonts", font_dir, sizeof(font_dir)) == PATHS_OK)
            fcfg.font_dir = font_dir;
        else if (asset_dir_exists(XBOING_INSTALLED_FONTS_DIR))
            fcfg.font_dir = XBOING_INSTALLED_FONTS_DIR;
        sdl2_font_status_t fs;
        ctx->font = sdl2_font_create(&fcfg, &fs);
        if (!ctx->font)
        {
            fprintf(stderr, "game_create: font creation failed: %s\n", sdl2_font_status_string(fs));
            goto fail;
        }
    }

    /* Audio (optional — game works without sound).  Same XDG-first
     * resolution as the texture and font subsystems. */
    char sound_dir[PATHS_MAX_PATH];
    if (ctx->config.sound)
    {
        sdl2_audio_config_t acfg = sdl2_audio_config_defaults();
        if (paths_install_data_dir(&ctx->paths, "sounds", sound_dir, sizeof(sound_dir)) == PATHS_OK)
            acfg.sound_dir = sound_dir;
        else if (asset_dir_exists(XBOING_INSTALLED_SOUNDS_DIR))
            acfg.sound_dir = XBOING_INSTALLED_SOUNDS_DIR;
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

    /* Ball system (callbacks wired by game_callbacks.c) */
    {
        ball_system_callbacks_t bcb = game_callbacks_ball();
        ball_system_status_t bs;
        ctx->ball = ball_system_create(&bcb, ctx, &bs);
        if (!ctx->ball)
        {
            fprintf(stderr, "game_create: ball system creation failed\n");
            goto fail;
        }
    }

    /* Gun system (callbacks wired by game_callbacks.c) */
    {
        gun_system_callbacks_t gcb = game_callbacks_gun();
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

    /* Bonus system (callbacks wired by game_callbacks.c) */
    {
        bonus_system_callbacks_t bcb = game_callbacks_bonus();
        ctx->bonus = bonus_system_create(&bcb, ctx);
        if (!ctx->bonus)
        {
            fprintf(stderr, "game_create: bonus system creation failed\n");
            goto fail;
        }
    }

    /* SFX system (callbacks wired by game_callbacks.c) */
    {
        sfx_system_callbacks_t scb = game_callbacks_sfx();
        ctx->sfx = sfx_system_create(&scb, ctx, NULL);
        if (!ctx->sfx)
        {
            fprintf(stderr, "game_create: sfx system creation failed\n");
            goto fail;
        }
        /* The visual special-effects system defaults on, so -nosfx
         * (config.sfx == false) must disable it here at startup;
         * otherwise the flag is parsed but never applied. */
        if (!ctx->config.sfx)
            sfx_system_set_enabled(ctx->sfx, 0);
    }

    /* EyeDude system (callbacks wired by game_callbacks.c) */
    {
        eyedude_system_callbacks_t ecb = game_callbacks_eyedude();
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

    /* Editor system (callbacks wired by game_callbacks.c) */
    {
        editor_system_callbacks_t ecb = game_callbacks_editor();
        char levels_dir_r[PATHS_MAX_PATH] = "levels";
        char levels_dir_w[PATHS_MAX_PATH] = "levels";
        paths_levels_dir_readable(&ctx->paths, levels_dir_r, sizeof(levels_dir_r));
        paths_levels_dir_writable(&ctx->paths, levels_dir_w, sizeof(levels_dir_w));
        ctx->editor =
            editor_system_create(&ecb, ctx, levels_dir_r, levels_dir_w, !ctx->config.sound);
        if (!ctx->editor)
        {
            fprintf(stderr, "game_create: editor system creation failed\n");
            goto fail;
        }
    }

    /* ---- Phase 5: UI sequencers ----------------------------------------- */

    /* Presents (callbacks wired by game_callbacks.c) */
    {
        presents_system_callbacks_t pcb = game_callbacks_presents();
        ctx->presents = presents_system_create(&pcb, ctx);
        if (!ctx->presents)
        {
            fprintf(stderr, "game_create: presents system creation failed\n");
            goto fail;
        }
    }

    /* Intro (callbacks wired by game_callbacks.c) */
    {
        intro_system_callbacks_t icb = game_callbacks_intro();
        ctx->intro = intro_system_create(&icb, ctx, NULL);
        if (!ctx->intro)
        {
            fprintf(stderr, "game_create: intro system creation failed\n");
            goto fail;
        }
    }

    /* Demo (callbacks wired by game_callbacks.c) */
    {
        demo_system_callbacks_t dcb = game_callbacks_demo();
        ctx->demo = demo_system_create(&dcb, ctx, NULL);
        if (!ctx->demo)
        {
            fprintf(stderr, "game_create: demo system creation failed\n");
            goto fail;
        }
    }

    /* Keys (callbacks wired by game_callbacks.c) */
    {
        keys_system_callbacks_t kcb = game_callbacks_keys();
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

    /* High score display (callbacks wired by game_callbacks.c) */
    {
        highscore_system_callbacks_t hcb = game_callbacks_highscore();
        ctx->highscore_display = highscore_system_create(&hcb, ctx);
        if (!ctx->highscore_display)
        {
            fprintf(stderr, "game_create: highscore display creation failed\n");
            goto fail;
        }
    }

    /* Register mode handlers with the state machine */
    game_modes_register(ctx);

    /* Give initial ammo */
    gun_system_set_ammo(ctx->gun, GUN_AMMO_PER_LEVEL);

    /* ---- Phase 6: Load initial level + place ball on paddle -------------
     * Pre-load level data so attract-mode renderers have something
     * to draw if they reach into level_system.  Do NOT advance the
     * background — original/file.c::SetupStage is the only call that
     * does so on game start, and start_new_game (game_modes.c:129)
     * is its modern equivalent.  Advancing here AND in start_new_game
     * skips bgrnd2 entirely; level 1 of a fresh process would render
     * with bgrnd3. */
    {
        int file_num = level_system_wrap_number(ctx->level_number);
        char filename[32];
        snprintf(filename, sizeof(filename), "level%02d.data", file_num);

        char level_path[PATHS_MAX_PATH];
        if (paths_level_file(&ctx->paths, filename, level_path, sizeof(level_path)) == PATHS_OK)
        {
            block_system_clear_all(ctx->block);
            level_system_load_file(ctx->level, level_path);
        }
        else
        {
            fprintf(stderr, "Warning: could not find level file: %s\n", filename);
        }
    }

    /* Place the first ball on the paddle */
    {
        ball_system_env_t env = {
            .frame = 0,
            .speed_level = ctx->config.speed,
            .paddle_pos = paddle_system_get_pos(ctx->paddle),
            .paddle_dx = 0,
            .paddle_size = paddle_system_get_size(ctx->paddle),
            .play_width = GAME_PLAY_WIDTH,
            .play_height = GAME_PLAY_HEIGHT,
            .col_width = GAME_COL_WIDTH,
            .row_height = GAME_ROW_HEIGHT,
        };
        ball_system_reset_start(ctx->ball, &env);
    }

    return ctx;

fail:
    game_destroy(ctx);
    return NULL;
}

/* =========================================================================
 * game_seed_rng_default
 *
 * Production seeding policy: time(NULL).  The library never calls
 * this on the caller's behalf — production main() invokes it once
 * before game_create(), tests choose their own srand() seed (or
 * choose not to seed).  See include/game_init.h for the contract.
 * ========================================================================= */

void game_seed_rng_default(void)
{
    srand((unsigned)time(NULL));
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

static const char *vc_presents_name(int s)
{
    switch (s)
    {
        case PRESENTS_STATE_FLAG:
            return "flag";
        case PRESENTS_STATE_TEXT1:
            return "text1";
        case PRESENTS_STATE_TEXT2:
            return "text2";
        case PRESENTS_STATE_TEXT3:
            return "text3";
        case PRESENTS_STATE_TEXT_CLEAR:
            return "text-clear";
        case PRESENTS_STATE_LETTERS:
            return "letters";
        case PRESENTS_STATE_SHINE:
            return "shine";
        case PRESENTS_STATE_SPECIAL_TEXT1:
            return "typewriter1";
        case PRESENTS_STATE_SPECIAL_TEXT2:
            return "typewriter2";
        case PRESENTS_STATE_SPECIAL_TEXT3:
            return "typewriter3";
        case PRESENTS_STATE_CLEAR:
            return "wipe";
        default:
            return NULL;
    }
}

static const char *vc_intro_name(int s)
{
    switch (s)
    {
        case INTRO_STATE_TITLE:
            return "title";
        case INTRO_STATE_BLOCKS:
            return "blocks";
        case INTRO_STATE_TEXT:
            return "text";
        case INTRO_STATE_EXPLODE:
            return "explode";
        default:
            return NULL;
    }
}

static const char *vc_instruct_name(int s)
{
    switch (s)
    {
        case INTRO_STATE_TITLE:
            return "title";
        case INTRO_STATE_TEXT:
            return "text";
        case INTRO_STATE_EXPLODE:
            return "sparkle";
        default:
            return NULL;
    }
}

static const char *vc_demo_name(int s)
{
    switch (s)
    {
        case DEMO_STATE_TITLE:
            return "title";
        case DEMO_STATE_BLOCKS:
            return "blocks";
        case DEMO_STATE_TEXT:
            return "text";
        case DEMO_STATE_SPARKLE:
            return "sparkle";
        case DEMO_STATE_WAIT:
            return "wait";
        default:
            return NULL;
    }
}

static const char *vc_keys_name(int s)
{
    switch (s)
    {
        case KEYS_STATE_TITLE:
            return "title";
        case KEYS_STATE_TEXT:
            return "text";
        case KEYS_STATE_SPARKLE:
            return "sparkle";
        case KEYS_STATE_WAIT:
            return "wait";
        default:
            return NULL;
    }
}

static const char *vc_highscore_name(int s)
{
    switch (s)
    {
        case HIGHSCORE_STATE_TITLE:
            return "title";
        case HIGHSCORE_STATE_SHOW:
            return "show";
        case HIGHSCORE_STATE_SPARKLE:
            return "sparkle";
        case HIGHSCORE_STATE_WAIT:
            return "wait";
        default:
            return NULL;
    }
}

static const char *vc_bonus_name(int s)
{
    switch (s)
    {
        case BONUS_STATE_TEXT:
            return "title";
        case BONUS_STATE_SCORE:
            return "score";
        case BONUS_STATE_BONUS:
            return "bonus";
        case BONUS_STATE_LEVEL:
            return "level";
        case BONUS_STATE_BULLET:
            return "bullets";
        case BONUS_STATE_TIME:
            return "time";
        case BONUS_STATE_HSCORE:
            return "hscore";
        case BONUS_STATE_END_TEXT:
            return "end-text";
        default:
            return NULL;
    }
}

static const char *vc_edit_name(int s)
{
    switch (s)
    {
        case EDITOR_STATE_NONE:
            return "editing";
        default:
            return NULL;
    }
}

static void vc_signal_modern(const char *mode_name, const char *substate, int seq)
{
    /* Pre-signal pause: the SDL_RenderPresent that ran in
     * game_render_frame queues a frame for the X server.  Under
     * XWayland+Mutter the actual on-screen update lags by up to
     * ~50 ms after present, especially on the first frame of a
     * newly-entered state.  Sleep BEFORE printing the signal so
     * the script's `import -window` sees the just-rendered frame,
     * not the previous one. */
    SDL_Delay(150);
    printf("XBOING_SNAPSHOT %s/%s/%03d\n", mode_name, substate, seq);
    fflush(stdout);
    /* Post-signal pause: hold the binary while the script's
     * ImageMagick `import` runs (typically ~50 ms). */
    SDL_Delay(200);
}

static void vc_check(game_ctx_t *ctx, int pre_presents, int pre_credits, int pre_intro,
                     int pre_demo, int pre_keys, int pre_highscore, int pre_bonus, int pre_edit)
{
    static sdl2_state_mode_t prev_mode = SDL2ST_NONE;
    static unsigned long next_capture_frame = 0;
    static int seq = 0;
    static const char *cur_subname = NULL;

    sdl2_state_mode_t mode = sdl2_state_current(ctx->state);
    unsigned long frame = sdl2_state_frame(ctx->state);

    /* Check mode transition BEFORE the active-mode gate — when
     * capturing a single mode, we need to detect the transition
     * away from that mode and exit, even though the new mode
     * isn't the one we're capturing. */
    if (mode != prev_mode)
    {
        if (prev_mode != SDL2ST_NONE && ctx->vc_mode != 99 && ctx->vc_mode == (int)prev_mode)
        {
            printf("XBOING_SNAPSHOT_DONE\n");
            fflush(stdout);
            exit(0);
        }
        prev_mode = mode;
        cur_subname = NULL;
        seq = 0;
        return;
    }

    int vc_active = (ctx->vc_mode == 99 || ctx->vc_mode == (int)mode);
    if (!vc_active)
        return;

    int pre_sub = -1;
    int post_sub = -1;
    const char *mode_str = NULL;

    if (mode == SDL2ST_PRESENTS)
    {
        pre_sub = pre_presents;
        post_sub = (int)presents_system_get_state(ctx->presents);
        mode_str = "presents";

        /* Credits stage changes are invisible to sub-state detection
         * (TEXT1/2/3 flash by within one update call). Use the
         * persistent credits_stage field instead. */
        int post_credits = presents_system_get_credits_stage(ctx->presents);
        if (post_credits != pre_credits && post_credits > 0)
        {
            static const char *const credit_names[] = {NULL, "text1", "text2", "text3"};
            const char *cn = credit_names[post_credits];
            if (cn && cn != cur_subname)
            {
                seq = 0;
                vc_signal_modern("presents", cn, seq);
                seq++;
                cur_subname = cn;
                next_capture_frame = frame + (unsigned long)ctx->vc_interval;
            }
        }
        else if (post_credits == 0 && pre_credits > 0)
        {
            const char *cn = "text-clear";
            if (cn != cur_subname)
            {
                seq = 0;
                vc_signal_modern("presents", cn, seq);
                seq++;
                cur_subname = cn;
                next_capture_frame = frame + (unsigned long)ctx->vc_interval;
            }
        }
    }
    else if (mode == SDL2ST_INTRO)
    {
        pre_sub = pre_intro;
        post_sub = (int)intro_system_get_state(ctx->intro);
        mode_str = "intro";
    }
    else if (mode == SDL2ST_INSTRUCT)
    {
        pre_sub = pre_intro;
        post_sub = (int)intro_system_get_state(ctx->intro);
        mode_str = "instruct";
    }
    else if (mode == SDL2ST_DEMO)
    {
        pre_sub = pre_demo;
        post_sub = (int)demo_system_get_state(ctx->demo);
        mode_str = "demo";
    }
    else if (mode == SDL2ST_PREVIEW)
    {
        pre_sub = pre_demo;
        post_sub = (int)demo_system_get_state(ctx->demo);
        mode_str = "preview";
    }
    else if (mode == SDL2ST_KEYS)
    {
        pre_sub = pre_keys;
        post_sub = (int)keys_system_get_state(ctx->keys);
        mode_str = "keys";
    }
    else if (mode == SDL2ST_KEYSEDIT)
    {
        pre_sub = pre_keys;
        post_sub = (int)keys_system_get_state(ctx->keys);
        mode_str = "keysedit";
    }
    else if (mode == SDL2ST_HIGHSCORE)
    {
        pre_sub = pre_highscore;
        post_sub = (int)highscore_system_get_state(ctx->highscore_display);
        mode_str = "highscore";
    }
    else if (mode == SDL2ST_BONUS)
    {
        /* The raw state is unobservable at render-frame granularity —
         * mode_bonus_update runs bonus_system_update 6× per tick so
         * LIVE substates flash by inside one render frame.  Use the
         * monotonic highest_reached query instead: it advances when
         * the state machine enters a new content state, and we can
         * detect that increment between render frames. */
        pre_sub = pre_bonus;
        post_sub = (int)bonus_system_get_highest_reached(ctx->bonus);
        mode_str = "bonus";
    }
    else if (mode == SDL2ST_EDIT)
    {
        pre_sub = pre_edit;
        post_sub = (int)editor_system_get_state(ctx->editor);
        mode_str = "editor";
    }

    if (!mode_str)
        return;

    const char *(*name_fn)(int) = NULL;
    if (mode == SDL2ST_PRESENTS)
        name_fn = vc_presents_name;
    else if (mode == SDL2ST_INTRO)
        name_fn = vc_intro_name;
    else if (mode == SDL2ST_INSTRUCT)
        name_fn = vc_instruct_name;
    else if (mode == SDL2ST_DEMO || mode == SDL2ST_PREVIEW)
        name_fn = vc_demo_name;
    else if (mode == SDL2ST_KEYS || mode == SDL2ST_KEYSEDIT)
        name_fn = vc_keys_name;
    else if (mode == SDL2ST_HIGHSCORE)
        name_fn = vc_highscore_name;
    else if (mode == SDL2ST_BONUS)
        name_fn = vc_bonus_name;
    else if (mode == SDL2ST_EDIT)
        name_fn = vc_edit_name;

    if (!name_fn)
        return;

    /* Content state ran this frame (pre != post). Signal pre name. */
    const char *pre_name = name_fn(pre_sub);
    if (pre_sub != post_sub && pre_name && pre_name != cur_subname)
    {
        seq = 0;
        vc_signal_modern(mode_str, pre_name, seq);
        seq++;
        cur_subname = pre_name;
        next_capture_frame = frame + (unsigned long)ctx->vc_interval;
    }

    /* Persistent state (pre == post, named) — interval sampling. */
    const char *post_name = name_fn(post_sub);
    if (pre_sub == post_sub && post_name)
    {
        if (post_name != cur_subname)
        {
            seq = 0;
            vc_signal_modern(mode_str, post_name, seq);
            seq++;
            cur_subname = post_name;
            next_capture_frame = frame + (unsigned long)ctx->vc_interval;
        }
        else if (frame >= next_capture_frame)
        {
            vc_signal_modern(mode_str, post_name, seq);
            seq++;
            next_capture_frame = frame + (unsigned long)ctx->vc_interval;
        }
    }
}

static int vc_pre_presents_state;
static int vc_pre_credits_stage;
static int vc_pre_intro_state;
static int vc_pre_demo_state;
static int vc_pre_keys_state;
static int vc_pre_highscore_state;
static int vc_pre_bonus_state;
static int vc_pre_edit_state;

static void stub_tick(void *user_data)
{
    game_ctx_t *ctx = user_data;

    vc_pre_presents_state = (int)presents_system_get_state(ctx->presents);
    vc_pre_credits_stage = presents_system_get_credits_stage(ctx->presents);
    vc_pre_intro_state = (int)intro_system_get_state(ctx->intro);
    vc_pre_demo_state = (int)demo_system_get_state(ctx->demo);
    vc_pre_keys_state = (int)keys_system_get_state(ctx->keys);
    vc_pre_highscore_state = (int)highscore_system_get_state(ctx->highscore_display);
    vc_pre_bonus_state = (int)bonus_system_get_highest_reached(ctx->bonus);
    vc_pre_edit_state = (int)editor_system_get_state(ctx->editor);

    sdl2_state_update(ctx->state);
}

static void stub_render(double alpha, void *user_data)
{
    game_ctx_t *ctx = user_data;
    sdl2_state_mode_t mode = sdl2_state_current(ctx->state);
    ctx->render_alpha = (mode == SDL2ST_PAUSE || mode == SDL2ST_DIALOGUE) ? 0.0 : alpha;
    game_render_frame(ctx);

    if (ctx->vc_mode >= 0)
        vc_check(ctx, vc_pre_presents_state, vc_pre_credits_stage, vc_pre_intro_state,
                 vc_pre_demo_state, vc_pre_keys_state, vc_pre_highscore_state, vc_pre_bonus_state,
                 vc_pre_edit_state);
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
