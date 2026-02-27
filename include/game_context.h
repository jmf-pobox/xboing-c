/*
 * game_context.h -- Master context struct for SDL2-based XBoing.
 *
 * Holds pointers to all module contexts, shared game state, and
 * persistence data.  Passed as void *user_data to every callback
 * in the system.  Created by game_create(), destroyed by game_destroy().
 *
 * No module includes here -- only forward declarations.  This keeps
 * compile times low and avoids circular header dependencies.
 */

#ifndef GAME_CONTEXT_H
#define GAME_CONTEXT_H

#include <stdbool.h>
#include <time.h>

#include "config_io.h"
#include "highscore_system.h" /* highscore_table_t (value type, needed inline) */
#include "paths.h"            /* paths_config_t (value type, needed inline) */

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
typedef struct highscore_system highscore_system_t;

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

    /* --- Game state (replaces legacy globals from main.c) ---------------- */
    int level_number;   /* Current level (1-based) */
    int lives_left;     /* Remaining lives */
    int start_level;    /* Level to start from (CLI or config) */
    bool game_active;   /* True while a game session is running */
    time_t game_start;  /* Timestamp when game session began */
    int paused_seconds; /* Total seconds spent paused */

    /* Bonus spawning state (from main.c:handleGameMode) */
    bool bonus_block_active; /* True while a bonus block is on the grid */
    int next_bonus_frame;    /* Frame at which next bonus block may spawn */

    /* Tilt state */
    int user_tilts; /* Remaining tilts this level */

    /* Debug / control flags */
    bool debug_mode;

} game_ctx_t;

#endif /* GAME_CONTEXT_H */
