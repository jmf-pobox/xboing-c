/*
 * game_context.h -- Master context struct for SDL2-based XBoing.
 *
 * Holds pointers to all module contexts, shared game state, and
 * persistence data.  Passed as void *user_data to every callback
 * in the system.  Created by game_create(), destroyed by game_destroy().
 *
 * Opaque module contexts are forward-declared (no headers pulled in),
 * which keeps compile times low and avoids circular dependencies.  The
 * only headers included are for value-type members stored inline in the
 * struct (config_io, highscore, paths, savegame_io) -- these must be
 * complete types, and none of them include game_context.h, so there is
 * no cycle.
 */

#ifndef GAME_CONTEXT_H
#define GAME_CONTEXT_H

#include <stdbool.h>
#include <time.h>

#include "config_io.h"
#include "highscore_system.h" /* highscore_table_t (value type, needed inline) */
#include "paths.h"            /* paths_config_t (value type, needed inline) */
#include "savegame_io.h"      /* savegame_data_t / savegame_level_t (value types, needed inline) */

/* =========================================================================
 * Forward declarations -- opaque pointers, no headers pulled in
 * ========================================================================= */

/* SDL2 platform modules */
typedef struct sdl2_renderer sdl2_renderer_t;
typedef struct sdl2_texture sdl2_texture_t;
typedef struct sdl2_font sdl2_font_t;
typedef struct sdl2_audio sdl2_audio_t;
typedef struct sdl2_input sdl2_input_t;
typedef struct sdl2_cursor sdl2_cursor_t;
typedef struct sdl2_state sdl2_state_t;
typedef struct sdl2_loop sdl2_loop_t;

/* Game system modules */
typedef struct ball_system ball_system_t;
typedef struct block_system block_system_t;
typedef struct paddle_system paddle_system_t;
typedef struct gun_system gun_system_t;
typedef struct score_system score_system_t;
typedef struct level_system level_system_t;
typedef struct special_system special_system_t;
typedef struct bonus_system bonus_system_t;
typedef struct sfx_system sfx_system_t;
typedef struct eyedude_system eyedude_system_t;
typedef struct message_system message_system_t;
typedef struct editor_system editor_system_t;

/* UI sequencer modules */
typedef struct presents_system presents_system_t;
typedef struct intro_system intro_system_t;
typedef struct demo_system demo_system_t;
typedef struct keys_system keys_system_t;
typedef struct dialogue_system dialogue_system_t;
/* highscore_system_t already typedef'd via highscore_system.h above */

/* =========================================================================
 * Play area geometry — original/include/stage.h PLAY_WIDTH/PLAY_HEIGHT
 * ========================================================================= */

#define GAME_PLAY_WIDTH 495
#define GAME_PLAY_HEIGHT 580
#define GAME_COL_WIDTH (GAME_PLAY_WIDTH / 9)
#define GAME_ROW_HEIGHT (GAME_PLAY_HEIGHT / 18)

/* =========================================================================
 * Master context
 * ========================================================================= */

typedef struct game_ctx
{
    /* --- SDL2 platform --------------------------------------------------- */
    sdl2_renderer_t *renderer;
    sdl2_texture_t *texture;
    sdl2_font_t *font;
    sdl2_audio_t *audio;
    sdl2_input_t *input;
    sdl2_cursor_t *cursor;
    sdl2_state_t *state;
    sdl2_loop_t *loop;

    /* --- Game systems ---------------------------------------------------- */
    ball_system_t *ball;
    block_system_t *block;
    paddle_system_t *paddle;
    gun_system_t *gun;
    score_system_t *score;
    level_system_t *level;
    special_system_t *special;
    bonus_system_t *bonus;
    sfx_system_t *sfx;
    eyedude_system_t *eyedude;
    message_system_t *message;
    editor_system_t *editor;

    /* --- UI sequencers --------------------------------------------------- */
    presents_system_t *presents;
    intro_system_t *intro;
    demo_system_t *demo;
    keys_system_t *keys;
    dialogue_system_t *dialogue;
    highscore_system_t *highscore_display;

    /* --- Persistence (value types, owned inline) ------------------------- */
    paths_config_t paths;
    config_data_t config;
    highscore_table_t hs_global;
    highscore_table_t hs_personal;

    highscore_type_t highscore_request_type;

    /* --- Game state (replaces legacy globals from main.c) ---------------- */
    int level_number;     /* Current level (1-based) */
    int lives_left;       /* Remaining lives */
    int start_level;      /* Level to start from (CLI or config) */
    bool game_active;     /* True while a game session is running */
    bool score_submitted; /* True after score inserted into highscore table */
    /* True once the active session has been seeded from a user-writable save
     * file (via -load or the in-game X-key path).  Disqualifies the session
     * from global Hall-of-Fame submission: save files are not integrity-
     * protected, so a local user could otherwise edit their save to forge
     * a shared score.  Personal-table inserts remain eligible — that file
     * is already under the user's control. */
    bool savegame_restored_session;
    time_t game_start;  /* Timestamp when game session began */
    int paused_seconds; /* Total seconds spent paused */

    /* Bonus spawning state (from main.c:handleGameMode) */
    bool bonus_block_active; /* True while a bonus block is on the grid */
    int next_bonus_frame;    /* Frame at which next bonus block may spawn */
    /* Cell + type of the currently-active spawned bonus/special block.
     * Mirrors original/main.c's `bonusRow`/`bonusCol` globals, populated
     * by AddBonusBlock/AddSpecialBlock (original/blocks.c:1084-1085,
     * 1116-1117).  bonus_type lets try_spawn_bonus (game_rules.c) tell
     * "player destroyed/collected it" (type at the cell no longer
     * matches) apart from "still waiting to expire".  Only meaningful
     * while bonus_block_active is true. */
    int bonus_row;
    int bonus_col;
    int bonus_type;

    /* Timer state */
    int time_bonus_total; /* Total time bonus from level file (seconds) */
    int time_remaining;   /* Seconds remaining on level timer */
    int timer_frame_acc;  /* Frame accumulator for 1-second countdown */

    /* Tilt state — original/include/main.h:85 */
#define GAME_MAX_TILTS 3
    int user_tilts;

    /* Bonus block counter — incremented on BONUS_BLK explosion finalize.
     * At count == 10, killer mode activates (matches original/blocks.c:1607). */
    int bonus_count;

    /* Render interpolation */
    double render_alpha; /* 0.0–1.0, fraction of tick elapsed since last physics step */

    /* Debug / control flags */
    bool debug_mode;

    /* Visual-capture: -1 = off, SDL2ST_* = single mode, 99 = all */
    int vc_mode;
    int vc_interval;

    /* Autoload: -load CLI flag asks main() to call
     * savegame_system_load and enter SDL2ST_GAME before the event
     * loop, bypassing the attract cycle. */
    bool autoload;

    /* Attract-mode display overrides (don't affect game state) */
    int attract_level_display; /* 0 = use real level_number */

    /* Editor Button3 (right-click) inspect override for game_render_score.
     * Gated to SDL2ST_EDIT only -- see game_render_score. */
    int editor_inspect_active;          /* 0 = show real score */
    unsigned long editor_inspect_value; /* last-queried hit points */

    /* Editor play-test session (docs/specs/2026-07-12-playtest-fidelity.md
     * S3.1/S3.5).  True for the duration of an EDIT->GAME->EDIT play-test
     * round-trip started by EDITOR_KEY_PLAYTEST; matches the original's
     * `mode == MODE_EDIT` staying true through SetupPlayTest/FinishPlayTest
     * (original/editor.c:587-645) with no dedicated flag needed there --
     * the modern port needs one because it re-enters a genuinely distinct
     * SDL2ST_GAME mode to reuse the real gameplay pipeline. */
    bool play_test_active;
    savegame_data_t play_test_snapshot_info;   /* pre-test board+session snapshot */
    savegame_level_t play_test_snapshot_level; /* pre-test block grid snapshot */

} game_ctx_t;

#endif /* GAME_CONTEXT_H */
