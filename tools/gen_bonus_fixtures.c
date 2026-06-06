/*
 * gen_bonus_fixtures.c — generate savegame v2 JSON fixtures for the
 * bonus-screen visual-fidelity capture flow.
 *
 * Each fixture represents a level frozen one tick before completion:
 * empty block grid (cells all unoccupied), with scenario-specific
 * score / level / time / ammo / lives / killer state.  When the
 * modern binary loads the fixture (via X-key while in MODE_GAME),
 * game_rules_check sees no required blocks remaining and transitions
 * to SDL2ST_BONUS on the next tick.  The capture script
 * (scripts/visual_capture.sh modern bonus) then snapshots each
 * substate as the bonus state machine runs.
 *
 * Usage:
 *   ./gen_bonus_fixtures <output-root>
 *
 * Output layout:
 *   <root>/scenario-1/xboing/save-info.dat
 *   <root>/scenario-1/xboing/save-level.dat
 *   <root>/scenario-2/...
 *   ...
 *
 * (The xboing/ subdir mirrors what paths_save_info resolves to
 * when XDG_DATA_HOME=<root>/scenario-N.)
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "paddle_system.h"
#include "savegame_io.h"

typedef struct
{
    int scenario;
    unsigned long score;
    unsigned long level;
    int start_level;
    int time_remaining;
    int num_bullets;
    int lives_left;
    int killer;
    int bonus_count;
} scenario_t;

/* Bonus-coin counts mirror the original-side force-entry helper in
 * original/main.c::setup_bonus_capture_scenario so the modern bonus
 * tally renders the same number of "B" sprites the original golden
 * shows. */
static const scenario_t SCENARIOS[] = {
    {1,  45000UL,  3UL,  1, 180,  8, 3, 0, 2},
    {2,  85000UL,  5UL,  1, 142, 12, 2, 0, 3},
    {3, 125000UL,  7UL,  1, 100, 20, 2, 1, 5},
    {4, 450000UL, 25UL,  1,  60, 30, 1, 0, 7},
};
#define NUM_SCENARIOS (sizeof(SCENARIOS) / sizeof(SCENARIOS[0]))

static int mkdir_p(const char *path)
{
    char buf[512];
    size_t len = strlen(path);
    if (len >= sizeof(buf))
    {
        /* Set errno so the caller's strerror(errno) reports the
         * actual cause rather than whatever value happened to be
         * in errno from a prior unrelated syscall. */
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(buf, path, len + 1);

    for (size_t i = 1; i < len; i++)
    {
        if (buf[i] == '/')
        {
            buf[i] = '\0';
            if (mkdir(buf, 0755) != 0 && errno != EEXIST)
                return -1;
            buf[i] = '/';
        }
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int write_one(const char *root, const scenario_t *s)
{
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/scenario-%d/xboing", root, s->scenario);
    if (mkdir_p(dir) != 0)
    {
        fprintf(stderr, "mkdir_p(%s): %s\n", dir, strerror(errno));
        return -1;
    }

    savegame_data_t info;
    savegame_io_init(&info);
    info.score = s->score;
    info.level = s->level;
    info.start_level = s->start_level;
    info.time_remaining = s->time_remaining;
    info.lives_left = s->lives_left;
    info.num_bullets = s->num_bullets;
    info.paddle_size_type = PADDLE_SIZE_HUGE;
    info.specials.killer = s->killer;
    info.bonus_count = s->bonus_count;
    /* level_time = total time bonus for the level.  For capture
     * scenarios we treat the scenario's time_remaining as both
     * the configured total and the just-completed-level value;
     * game_render_timer reads ctx->time_bonus_total (restored from
     * info.level_time) to decide whether to display the clock. */
    info.level_time = s->time_remaining;

    /* Empty level grid: cells stay unoccupied (savegame_level_init
     * zeroes everything).  block_system_still_active returns false
     * on load, game_rules_check transitions to SDL2ST_BONUS next
     * tick. */
    savegame_level_t lvl;
    savegame_level_init(&lvl);
    snprintf(lvl.title, sizeof(lvl.title), "Bonus capture scenario %d", s->scenario);
    lvl.time_bonus = s->time_remaining;

    char info_path[1100];
    char level_path[1100];
    snprintf(info_path, sizeof(info_path), "%s/save-info.dat", dir);
    snprintf(level_path, sizeof(level_path), "%s/save-level.dat", dir);

    if (savegame_io_write(info_path, &info) != SAVEGAME_IO_OK)
    {
        fprintf(stderr, "savegame_io_write(%s) failed\n", info_path);
        return -1;
    }
    if (savegame_level_write(level_path, &lvl) != SAVEGAME_IO_OK)
    {
        fprintf(stderr, "savegame_level_write(%s) failed\n", level_path);
        return -1;
    }
    printf("scenario %d -> %s\n", s->scenario, dir);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <output-root>\n", argv[0]);
        return 1;
    }
    for (size_t i = 0; i < NUM_SCENARIOS; i++)
    {
        if (write_one(argv[1], &SCENARIOS[i]) != 0)
            return 1;
    }
    return 0;
}
