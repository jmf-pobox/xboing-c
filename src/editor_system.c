/*
 * editor_system.c — Pure C level editor state machine.
 *
 * Extracted from legacy editor.c (1199 lines of X11-coupled code) into
 * a callback-based module with zero platform dependencies.
 *
 * See docs/DESIGN.md for design rationale.
 */

#include "editor_system.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* =========================================================================
 * Internal constants
 * ========================================================================= */

#define COL_WIDTH (EDITOR_PLAY_WIDTH / EDITOR_MAX_COL_EDIT)
#define ROW_HEIGHT (EDITOR_PLAY_HEIGHT / MAX_ROW)

/* =========================================================================
 * Internal types
 * ========================================================================= */

struct editor_system
{
    editor_system_callbacks_t cb;
    void *user_data;

    editor_state_t state;
    editor_state_t wait_mode; /* State to resume after WAIT */
    int waiting_frame;        /* Target frame for WAIT */

    /* Palette */
    editor_palette_entry_t palette[EDITOR_MAX_PALETTE];
    int palette_count;
    int selected_palette; /* Index into palette[] */

    /* Draw state */
    editor_draw_action_t draw_action;
    int old_col; /* Last drawn column (-1 = none) */
    int old_row; /* Last drawn row (-1 = none) */

    /* Level metadata */
    int level_number;
    int modified;
    char level_title[EDITOR_LEVEL_NAME_MAX];

    /* Configuration */
    char levels_dir_readable[512]; /* for load operations */
    char levels_dir_writable[512]; /* for save operations */
    int no_sound;
};

/* =========================================================================
 * Helpers
 * ========================================================================= */

static void play_sound(const editor_system_t *ctx, const char *name, int volume)
{
    if (ctx->no_sound)
        return;
    if (ctx->cb.on_sound != NULL)
        ctx->cb.on_sound(name, volume, ctx->user_data);
}

static void show_message(const editor_system_t *ctx, const char *msg, int sticky)
{
    if (ctx->cb.on_message != NULL)
        ctx->cb.on_message(msg, sticky, ctx->user_data);
}

static int pixel_to_col(int x)
{
    return x / COL_WIDTH;
}

static int pixel_to_row(int y)
{
    return y / ROW_HEIGHT;
}

static int in_editable_bounds(int row, int col)
{
    return row >= 0 && row < EDITOR_MAX_ROW_EDIT && col >= 0 && col < EDITOR_MAX_COL_EDIT;
}

/*
 * Normalize random blocks.
 *
 * In the legacy editor, this walked the internal board representation and,
 * for any cell with the "random" flag set, forced its block_type back to
 * RANDOM_BLK so that board transforms operated on RANDOM_BLK values rather
 * than on a resolved concrete type.
 *
 * In this callback-based implementation, editor_cell_t does not expose a
 * "random" flag, and the normalization of random blocks (if needed) is
 * handled by the integration layer via the callback interface / level
 * loading path.  As a result, this function is intentionally a no-op and
 * exists only for API completeness and to mirror the legacy structure.
 */
static void normalize_random_blocks(editor_system_t *ctx)
{
    (void)ctx; /* Normalization is delegated to the integration layer. */
}

static void place_block(editor_system_t *ctx, int row, int col, int block_type, int counter_slide,
                        int visible)
{
    if (ctx->cb.on_add_block != NULL)
        ctx->cb.on_add_block(row, col, block_type, counter_slide, visible, ctx->user_data);
}

static void erase_block(editor_system_t *ctx, int row, int col)
{
    if (ctx->cb.on_erase_block != NULL)
        ctx->cb.on_erase_block(row, col, ctx->user_data);
}

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/*
 * Recursive mkdir for a two-level-deep path.
 * Splits on '/', calls mkdir() for each component, treats EEXIST as success.
 * Returns 0 on success, -1 on failure (errno set).
 */
static int mkdir_p(const char *path, mode_t mode)
{
    char tmp[512];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp))
        return -1;
    memcpy(tmp, path, len + 1);

    /* Strip trailing slash. */
    if (tmp[len - 1] == '/')
        tmp[--len] = '\0';

    for (size_t i = 1; i <= len; i++)
    {
        if (tmp[i] == '/' || tmp[i] == '\0')
        {
            char saved = tmp[i];
            tmp[i] = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST)
                return -1;
            tmp[i] = saved;
        }
    }
    return 0;
}

/* =========================================================================
 * Lifecycle
 * ========================================================================= */

editor_system_t *editor_system_create(const editor_system_callbacks_t *callbacks, void *user_data,
                                      const char *levels_dir_readable,
                                      const char *levels_dir_writable, int no_sound)
{
    editor_system_t *ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
        return NULL;

    if (callbacks != NULL)
        ctx->cb = *callbacks;

    ctx->user_data = user_data;
    ctx->state = EDITOR_STATE_LEVEL;
    ctx->selected_palette = 0;
    ctx->draw_action = EDITOR_ACTION_NOP;
    ctx->old_col = -1;
    ctx->old_row = -1;
    ctx->modified = 0;
    ctx->level_number = 0;
    ctx->no_sound = no_sound;

    if (levels_dir_readable != NULL)
        snprintf(ctx->levels_dir_readable, sizeof(ctx->levels_dir_readable), "%s",
                 levels_dir_readable);
    if (levels_dir_writable != NULL)
        snprintf(ctx->levels_dir_writable, sizeof(ctx->levels_dir_writable), "%s",
                 levels_dir_writable);

    return ctx;
}

void editor_system_destroy(editor_system_t *ctx)
{
    free(ctx);
}

/* =========================================================================
 * State machine
 * ========================================================================= */

static void do_load_level(editor_system_t *ctx)
{
    char path[1024];

    snprintf(path, sizeof(path), "%s/editor.data", ctx->levels_dir_readable);

    if (ctx->cb.on_load_level != NULL)
    {
        if (!ctx->cb.on_load_level(path, ctx->user_data))
        {
            if (ctx->cb.on_error != NULL)
                ctx->cb.on_error("Sorry, invalid level specified.", ctx->user_data);
            return;
        }
    }

    show_message(ctx, "<< Level Editor >>", 0);
    ctx->modified = 0;
}

void editor_system_update(editor_system_t *ctx, int frame)
{
    if (ctx == NULL)
        return;

    switch (ctx->state)
    {
        case EDITOR_STATE_LEVEL:
            do_load_level(ctx);
            ctx->state = EDITOR_STATE_NONE;
            break;

        case EDITOR_STATE_FINISH:
            play_sound(ctx, "evillaugh", 50);
            if (ctx->cb.on_finish != NULL)
                ctx->cb.on_finish(ctx->user_data);
            break;

        case EDITOR_STATE_WAIT:
            if (frame == ctx->waiting_frame)
                ctx->state = ctx->wait_mode;
            break;

        case EDITOR_STATE_TEST:
            /* Play-test logic is handled by the integration layer.
             * The editor system just tracks the state. */
            break;

        case EDITOR_STATE_NONE:
        default:
            break;
    }
}

editor_state_t editor_system_get_state(const editor_system_t *ctx)
{
    if (ctx == NULL)
        return EDITOR_STATE_LEVEL;
    return ctx->state;
}

void editor_system_reset(editor_system_t *ctx)
{
    if (ctx == NULL)
        return;
    ctx->state = EDITOR_STATE_LEVEL;
    ctx->draw_action = EDITOR_ACTION_NOP;
    ctx->old_col = -1;
    ctx->old_row = -1;
    ctx->modified = 0;
    ctx->level_number = 0;
}

/* =========================================================================
 * Palette
 * ========================================================================= */

int editor_system_init_palette(editor_system_t *ctx, int block_count)
{
    int k;

    if (ctx == NULL)
        return 0;

    ctx->palette_count = 0;

    /* Add static block types */
    for (int i = 0; i < block_count && ctx->palette_count < EDITOR_MAX_PALETTE; i++)
    {
        ctx->palette[ctx->palette_count].block_type = i;
        ctx->palette[ctx->palette_count].counter_slide = 0;
        ctx->palette_count++;
    }

    /* Add counter block variants (slides 1-5) */
    for (k = 1; k <= 5 && ctx->palette_count < EDITOR_MAX_PALETTE; k++)
    {
        ctx->palette[ctx->palette_count].block_type = COUNTER_BLK;
        ctx->palette[ctx->palette_count].counter_slide = k;
        ctx->palette_count++;
    }

    ctx->selected_palette = 0;
    return ctx->palette_count;
}

const editor_palette_entry_t *editor_system_get_palette_entry(const editor_system_t *ctx, int index)
{
    if (ctx == NULL || index < 0 || index >= ctx->palette_count)
        return NULL;
    return &ctx->palette[index];
}

int editor_system_get_palette_count(const editor_system_t *ctx)
{
    if (ctx == NULL)
        return 0;
    return ctx->palette_count;
}

int editor_system_select_palette(editor_system_t *ctx, int index)
{
    if (ctx == NULL || index < 0 || index >= ctx->palette_count)
        return -1;
    ctx->selected_palette = index;
    return 0;
}

int editor_system_get_selected_palette(const editor_system_t *ctx)
{
    if (ctx == NULL)
        return 0;
    return ctx->selected_palette;
}

/* =========================================================================
 * Grid editing
 * ========================================================================= */

editor_draw_action_t editor_system_mouse_button(editor_system_t *ctx, int x, int y, int button,
                                                int pressed)
{
    if (ctx == NULL)
        return EDITOR_ACTION_NOP;

    /* In play-test mode, mouse is handled by the integration layer */
    if (ctx->state == EDITOR_STATE_TEST)
        return EDITOR_ACTION_NOP;

    /* Bounds check pixel coordinates */
    if (x < 0 || x >= EDITOR_PLAY_WIDTH || y < 0 || y >= EDITOR_PLAY_HEIGHT)
        return ctx->draw_action;

    int col = pixel_to_col(x);
    int row = pixel_to_row(y);

    if (!in_editable_bounds(row, col))
        return ctx->draw_action;

    if (pressed)
    {
        switch (button)
        {
            case 1: /* Left button: draw */
            {
                const editor_palette_entry_t *entry = &ctx->palette[ctx->selected_palette];
                erase_block(ctx, row, col);
                place_block(ctx, row, col, entry->block_type, entry->counter_slide, 1);
                ctx->draw_action = EDITOR_ACTION_DRAW;
                ctx->old_col = col;
                ctx->old_row = row;
                ctx->modified = 1;
                play_sound(ctx, "bonus", 20);
                break;
            }

            case 2: /* Middle button: erase */
                erase_block(ctx, row, col);
                ctx->draw_action = EDITOR_ACTION_ERASE;
                ctx->old_col = col;
                ctx->old_row = row;
                ctx->modified = 1;
                play_sound(ctx, "bonus", 20);
                break;

            case 3: /* Right button: inspect (no-op in pure logic) */
                ctx->draw_action = EDITOR_ACTION_NOP;
                break;

            default:
                break;
        }
    }
    else
    {
        /* Button released */
        ctx->draw_action = EDITOR_ACTION_NOP;
    }

    return ctx->draw_action;
}

void editor_system_mouse_motion(editor_system_t *ctx, int x, int y)
{
    if (ctx == NULL)
        return;

    if (x < 0 || x >= EDITOR_PLAY_WIDTH || y < 0 || y >= EDITOR_PLAY_HEIGHT)
    {
        ctx->draw_action = EDITOR_ACTION_NOP;
        return;
    }

    int col = pixel_to_col(x);
    int row = pixel_to_row(y);

    if (!in_editable_bounds(row, col))
        return;

    switch (ctx->draw_action)
    {
        case EDITOR_ACTION_DRAW:
            if (ctx->old_col != col || ctx->old_row != row)
            {
                const editor_palette_entry_t *entry = &ctx->palette[ctx->selected_palette];
                erase_block(ctx, row, col);
                place_block(ctx, row, col, entry->block_type, entry->counter_slide, 1);
                ctx->old_col = col;
                ctx->old_row = row;
                ctx->modified = 1;
                play_sound(ctx, "bonus", 20);
            }
            break;

        case EDITOR_ACTION_ERASE:
            if (ctx->old_col != col || ctx->old_row != row)
            {
                erase_block(ctx, row, col);
                ctx->old_col = col;
                ctx->old_row = row;
                ctx->modified = 1;
                play_sound(ctx, "bonus", 20);
            }
            break;

        case EDITOR_ACTION_NOP:
        default:
            break;
    }
}

/* =========================================================================
 * Board transforms
 * ========================================================================= */

void editor_system_flip_horizontal(editor_system_t *ctx)
{
    editor_cell_t left_cells[EDITOR_MAX_ROW_EDIT];
    editor_cell_t right_cells[EDITOR_MAX_ROW_EDIT];

    if (ctx == NULL || ctx->cb.query_cell == NULL)
        return;

    play_sound(ctx, "wzzz", 50);
    normalize_random_blocks(ctx);

    for (int c = 0; c < EDITOR_MAX_COL_EDIT / 2; c++)
    {
        int mirror_c = EDITOR_MAX_COL_EDIT - c - 1;

        /* Read both columns */
        for (int r = 0; r < EDITOR_MAX_ROW_EDIT; r++)
        {
            if (!ctx->cb.query_cell(r, c, &left_cells[r], ctx->user_data))
            {
                left_cells[r].occupied = 0;
                left_cells[r].block_type = NONE_BLK;
                left_cells[r].counter_slide = 0;
            }
            if (!ctx->cb.query_cell(r, mirror_c, &right_cells[r], ctx->user_data))
            {
                right_cells[r].occupied = 0;
                right_cells[r].block_type = NONE_BLK;
                right_cells[r].counter_slide = 0;
            }
        }

        /* Swap: place right column data into left position and vice versa */
        for (int r = 0; r < EDITOR_MAX_ROW_EDIT; r++)
        {
            if (right_cells[r].occupied)
                place_block(ctx, r, c, right_cells[r].block_type, right_cells[r].counter_slide, 0);
            else
                erase_block(ctx, r, c);

            if (left_cells[r].occupied)
                place_block(ctx, r, mirror_c, left_cells[r].block_type, left_cells[r].counter_slide,
                            0);
            else
                erase_block(ctx, r, mirror_c);
        }
    }

    normalize_random_blocks(ctx);
    ctx->modified = 1;
}

void editor_system_flip_vertical(editor_system_t *ctx)
{
    editor_cell_t top_cells[EDITOR_MAX_COL_EDIT];
    editor_cell_t bottom_cells[EDITOR_MAX_COL_EDIT];

    if (ctx == NULL || ctx->cb.query_cell == NULL)
        return;

    play_sound(ctx, "wzzz2", 50);
    normalize_random_blocks(ctx);

    for (int r = 0; r < EDITOR_MAX_ROW_EDIT / 2; r++)
    {
        int mirror_r = EDITOR_MAX_ROW_EDIT - r - 1;

        /* Read both rows */
        for (int c = 0; c < EDITOR_MAX_COL_EDIT; c++)
        {
            if (!ctx->cb.query_cell(r, c, &top_cells[c], ctx->user_data))
            {
                top_cells[c].occupied = 0;
                top_cells[c].block_type = NONE_BLK;
                top_cells[c].counter_slide = 0;
            }
            if (!ctx->cb.query_cell(mirror_r, c, &bottom_cells[c], ctx->user_data))
            {
                bottom_cells[c].occupied = 0;
                bottom_cells[c].block_type = NONE_BLK;
                bottom_cells[c].counter_slide = 0;
            }
        }

        /* Swap */
        for (int c = 0; c < EDITOR_MAX_COL_EDIT; c++)
        {
            if (bottom_cells[c].occupied)
                place_block(ctx, r, c, bottom_cells[c].block_type, bottom_cells[c].counter_slide,
                            0);
            else
                erase_block(ctx, r, c);

            if (top_cells[c].occupied)
                place_block(ctx, mirror_r, c, top_cells[c].block_type, top_cells[c].counter_slide,
                            0);
            else
                erase_block(ctx, mirror_r, c);
        }
    }

    normalize_random_blocks(ctx);
    ctx->modified = 1;
}

void editor_system_scroll_horizontal(editor_system_t *ctx)
{
    editor_cell_t saved[EDITOR_MAX_ROW_EDIT];
    editor_cell_t current[EDITOR_MAX_ROW_EDIT];

    if (ctx == NULL || ctx->cb.query_cell == NULL)
        return;

    play_sound(ctx, "sticky", 50);
    normalize_random_blocks(ctx);

    /* Save leftmost column (col 0) — it will be overwritten first */
    for (int r = 0; r < EDITOR_MAX_ROW_EDIT; r++)
    {
        if (!ctx->cb.query_cell(r, 0, &saved[r], ctx->user_data))
        {
            saved[r].occupied = 0;
            saved[r].block_type = NONE_BLK;
            saved[r].counter_slide = 0;
        }
    }

    /* Place rightmost column (col MAX-1) into col 0 */
    for (int r = 0; r < EDITOR_MAX_ROW_EDIT; r++)
    {
        editor_cell_t cell;
        if (ctx->cb.query_cell(r, EDITOR_MAX_COL_EDIT - 1, &cell, ctx->user_data) && cell.occupied)
            place_block(ctx, r, 0, cell.block_type, cell.counter_slide, 0);
        else
            erase_block(ctx, r, 0);
    }

    /* Shift remaining columns right: col c gets old col c-1 data */
    for (int c = 1; c < EDITOR_MAX_COL_EDIT; c++)
    {
        /* Read current col c before overwriting */
        for (int r = 0; r < EDITOR_MAX_ROW_EDIT; r++)
        {
            if (!ctx->cb.query_cell(r, c, &current[r], ctx->user_data))
            {
                current[r].occupied = 0;
                current[r].block_type = NONE_BLK;
                current[r].counter_slide = 0;
            }
        }

        /* Write saved (previous col's data) into col c */
        for (int r = 0; r < EDITOR_MAX_ROW_EDIT; r++)
        {
            if (saved[r].occupied)
                place_block(ctx, r, c, saved[r].block_type, saved[r].counter_slide, 0);
            else
                erase_block(ctx, r, c);
        }

        /* Current becomes saved for next iteration */
        memcpy(saved, current, sizeof(saved));
    }

    normalize_random_blocks(ctx);
    ctx->modified = 1;
}

void editor_system_scroll_vertical(editor_system_t *ctx)
{
    editor_cell_t saved[EDITOR_MAX_COL_EDIT];
    editor_cell_t current[EDITOR_MAX_COL_EDIT];

    if (ctx == NULL || ctx->cb.query_cell == NULL)
        return;

    play_sound(ctx, "sticky", 50);
    normalize_random_blocks(ctx);

    /* Save topmost row (row 0) — it will be overwritten first */
    for (int c = 0; c < EDITOR_MAX_COL_EDIT; c++)
    {
        if (!ctx->cb.query_cell(0, c, &saved[c], ctx->user_data))
        {
            saved[c].occupied = 0;
            saved[c].block_type = NONE_BLK;
            saved[c].counter_slide = 0;
        }
    }

    /* Place bottommost row (row MAX-1) into row 0 */
    for (int c = 0; c < EDITOR_MAX_COL_EDIT; c++)
    {
        editor_cell_t cell;
        if (ctx->cb.query_cell(EDITOR_MAX_ROW_EDIT - 1, c, &cell, ctx->user_data) && cell.occupied)
            place_block(ctx, 0, c, cell.block_type, cell.counter_slide, 0);
        else
            erase_block(ctx, 0, c);
    }

    /* Shift remaining rows down: row r gets old row r-1 data */
    for (int r = 1; r < EDITOR_MAX_ROW_EDIT; r++)
    {
        /* Read current row r before overwriting */
        for (int c = 0; c < EDITOR_MAX_COL_EDIT; c++)
        {
            if (!ctx->cb.query_cell(r, c, &current[c], ctx->user_data))
            {
                current[c].occupied = 0;
                current[c].block_type = NONE_BLK;
                current[c].counter_slide = 0;
            }
        }

        /* Write saved (previous row's data) into row r */
        for (int c = 0; c < EDITOR_MAX_COL_EDIT; c++)
        {
            if (saved[c].occupied)
                place_block(ctx, r, c, saved[c].block_type, saved[c].counter_slide, 0);
            else
                erase_block(ctx, r, c);
        }

        /* Current becomes saved for next iteration */
        memcpy(saved, current, sizeof(saved));
    }

    normalize_random_blocks(ctx);
    ctx->modified = 1;
}

void editor_system_clear_grid(editor_system_t *ctx)
{
    if (ctx == NULL)
        return;

    if (ctx->cb.on_clear_grid != NULL)
        ctx->cb.on_clear_grid(ctx->user_data);

    ctx->modified = 1;
}

/* =========================================================================
 * Keyboard commands
 * ========================================================================= */

static void do_load(editor_system_t *ctx)
{
    char str[80];

    if (ctx->cb.on_input_dialogue == NULL)
        return;

    snprintf(str, sizeof(str), "Level range is [1-%d]", EDITOR_MAX_LEVELS);
    show_message(ctx, str, 0);

    const char *input =
        ctx->cb.on_input_dialogue("Input load level number please.", 1, ctx->user_data);
    if (input == NULL || input[0] == '\0')
        return;

    int num = atoi(input);
    if (num > 0 && num <= EDITOR_MAX_LEVELS)
    {
        char path[1024];
        snprintf(path, sizeof(path), "%s/level%02d.data", ctx->levels_dir_readable, num);

        if (ctx->cb.on_load_level != NULL && ctx->cb.on_load_level(path, ctx->user_data))
        {
            normalize_random_blocks(ctx);
            ctx->level_number = num;
            ctx->modified = 0;

            snprintf(str, sizeof(str), "Editing level %d", num);
            show_message(ctx, str, 0);
        }
        else if (ctx->cb.on_error != NULL)
        {
            ctx->cb.on_error("Sorry, invalid level specified.", ctx->user_data);
        }
    }
    else
    {
        snprintf(str, sizeof(str), "Invalid - level range [1-%d]", EDITOR_MAX_LEVELS);
        show_message(ctx, str, 1);
    }
}

static void do_save(editor_system_t *ctx)
{
    char str[80];

    if (ctx->cb.on_input_dialogue == NULL)
        return;

    snprintf(str, sizeof(str), "Level range is [1-%d]", EDITOR_MAX_LEVELS);
    show_message(ctx, str, 0);

    const char *input =
        ctx->cb.on_input_dialogue("Input save level number please.", 1, ctx->user_data);
    if (input == NULL || input[0] == '\0')
        return;

    int num = atoi(input);
    if (num > 0 && num <= EDITOR_MAX_LEVELS)
    {
        /* Ensure writable dir exists before first save. */
        if (mkdir_p(ctx->levels_dir_writable, 0755) != 0)
        {
            if (ctx->cb.on_error != NULL)
                ctx->cb.on_error("Sorry, unable to create levels directory.", ctx->user_data);
            return;
        }

        char path[1024];
        snprintf(path, sizeof(path), "%s/level%02d.data", ctx->levels_dir_writable, num);

        if (ctx->cb.on_save_level != NULL && ctx->cb.on_save_level(path, ctx->user_data))
        {
            ctx->level_number = num;
            ctx->modified = 0;

            snprintf(str, sizeof(str), "Level %d saved.", num);
            show_message(ctx, str, 0);
        }
        else if (ctx->cb.on_error != NULL)
        {
            ctx->cb.on_error("Sorry, unable to save level.", ctx->user_data);
        }
    }
    else
    {
        snprintf(str, sizeof(str), "Invalid - level range [1-%d]", EDITOR_MAX_LEVELS);
        show_message(ctx, str, 1);
    }
}

static void do_set_time(editor_system_t *ctx)
{
    if (ctx->cb.on_input_dialogue == NULL)
        return;

    const char *input = ctx->cb.on_input_dialogue("Input game time in seconds.", 1, ctx->user_data);
    if (input == NULL || input[0] == '\0')
        return;

    int num = atoi(input);
    if (num > 0 && num <= EDITOR_MAX_TIME)
    {
        if (ctx->cb.on_set_time != NULL)
            ctx->cb.on_set_time(num, ctx->user_data);
        show_message(ctx, "Time limit adjusted", 1);
        ctx->modified = 1;
    }
    else
    {
        char str[80];
        snprintf(str, sizeof(str), "Invalid - time range [1-%d]", EDITOR_MAX_TIME);
        show_message(ctx, str, 1);
    }
}

static void do_set_name(editor_system_t *ctx)
{
    char str[80];
    const char *input;

    if (ctx->cb.on_input_dialogue == NULL)
        return;

    snprintf(str, sizeof(str), "Name: %s", ctx->level_title);
    show_message(ctx, str, 0);

    input = ctx->cb.on_input_dialogue("Input new name for level please.", 0, ctx->user_data);
    if (input == NULL || input[0] == '\0')
        return;

    if (strlen(input) >= EDITOR_LEVEL_NAME_MAX)
    {
        show_message(ctx, "Level name too long.", 1);
        return;
    }

    snprintf(ctx->level_title, sizeof(ctx->level_title), "%s", input);
    show_message(ctx, "Level name adjusted", 1);
    ctx->modified = 1;
}

static void handle_editor_key(editor_system_t *ctx, editor_key_t key)
{
    switch (key)
    {
        case EDITOR_KEY_QUIT:
            if (ctx->modified)
            {
                if (ctx->cb.on_yes_no_dialogue != NULL &&
                    ctx->cb.on_yes_no_dialogue("Unsaved work, exit editor? [y/n]", ctx->user_data))
                    ctx->state = EDITOR_STATE_FINISH;
            }
            else if (ctx->cb.on_yes_no_dialogue != NULL &&
                     ctx->cb.on_yes_no_dialogue("Exit the level editor? [y/n]", ctx->user_data))
            {
                ctx->state = EDITOR_STATE_FINISH;
            }
            break;

        case EDITOR_KEY_REDRAW:
            show_message(ctx, "Redraw", 1);
            break;

        case EDITOR_KEY_LOAD:
            if (ctx->modified)
            {
                if (ctx->cb.on_yes_no_dialogue != NULL &&
                    ctx->cb.on_yes_no_dialogue("Unsaved work, continue load? [y/n]",
                                               ctx->user_data))
                    do_load(ctx);
            }
            else
            {
                do_load(ctx);
            }
            break;

        case EDITOR_KEY_SAVE:
            do_save(ctx);
            break;

        case EDITOR_KEY_TIME:
            do_set_time(ctx);
            break;

        case EDITOR_KEY_NAME:
            do_set_name(ctx);
            break;

        case EDITOR_KEY_PLAYTEST:
            ctx->state = EDITOR_STATE_TEST;
            show_message(ctx, "Play test level", 1);
            if (ctx->cb.on_playtest_start != NULL)
                ctx->cb.on_playtest_start(ctx->user_data);
            break;

        case EDITOR_KEY_CLEAR:
            if (ctx->cb.on_yes_no_dialogue != NULL &&
                ctx->cb.on_yes_no_dialogue("Clear this level? [y/n]", ctx->user_data))
            {
                editor_system_clear_grid(ctx);
                show_message(ctx, "Clear level", 1);
            }
            break;

        case EDITOR_KEY_FLIP_H:
            show_message(ctx, "Flip Horizontal", 1);
            editor_system_flip_horizontal(ctx);
            break;

        case EDITOR_KEY_SCROLL_H:
            show_message(ctx, "Scroll Horizontal", 1);
            editor_system_scroll_horizontal(ctx);
            break;

        case EDITOR_KEY_FLIP_V:
            show_message(ctx, "Flip Vertical", 1);
            editor_system_flip_vertical(ctx);
            break;

        case EDITOR_KEY_SCROLL_V:
            show_message(ctx, "Scroll Vertical", 1);
            editor_system_scroll_vertical(ctx);
            break;

        default:
            break;
    }
}

static void handle_playtest_key(editor_system_t *ctx, editor_key_t key)
{
    switch (key)
    {
        case EDITOR_KEY_PLAYTEST:
            /* P key toggles play-test off */
            ctx->state = EDITOR_STATE_NONE;
            if (ctx->cb.on_playtest_end != NULL)
                ctx->cb.on_playtest_end(ctx->user_data);
            break;

        case EDITOR_KEY_PADDLE_LEFT:
        case EDITOR_KEY_PADDLE_RIGHT:
        case EDITOR_KEY_SHOOT:
            /* Handled by integration layer */
            break;

        default:
            break;
    }
}

void editor_system_key_input(editor_system_t *ctx, editor_key_t key)
{
    if (ctx == NULL)
        return;

    if (ctx->state == EDITOR_STATE_TEST)
        handle_playtest_key(ctx, key);
    else
        handle_editor_key(ctx, key);
}

/* =========================================================================
 * Queries
 * ========================================================================= */

int editor_system_is_modified(const editor_system_t *ctx)
{
    if (ctx == NULL)
        return 0;
    return ctx->modified;
}

editor_draw_action_t editor_system_get_draw_action(const editor_system_t *ctx)
{
    if (ctx == NULL)
        return EDITOR_ACTION_NOP;
    return ctx->draw_action;
}

int editor_system_get_level_number(const editor_system_t *ctx)
{
    if (ctx == NULL)
        return 0;
    return ctx->level_number;
}

const char *editor_system_get_level_title(const editor_system_t *ctx)
{
    if (ctx == NULL)
        return "";
    return ctx->level_title;
}
