/*
 * savegame_system.h — full mid-level state save/load orchestration.
 *
 * Phase 3 of savegame v2 (docs/specs/2026-05-28-savegame-v2.md).
 * Composes savegame_io with per-subsystem accessors to produce a
 * complete snapshot of player + game + per-system state, and to
 * restore it on load.
 *
 * `capture` / `restore` are pure transforms (no I/O) — they read
 * from / write to subsystem contexts, suitable for test fixtures.
 * `save` / `load` / `autosave` layer I/O and a user-visible message
 * on top.
 */

#ifndef SAVEGAME_SYSTEM_H
#define SAVEGAME_SYSTEM_H

#include "game_context.h"
#include "savegame_io.h"

/*
 * Capture the current state of the game context into the two
 * savegame structs.  Pure: no file I/O, no user message, no audio.
 *
 * `out_info` must be non-NULL.  `out_level` may be NULL when the
 * caller does not need the block grid snapshot (used by autosave).
 *
 * After this call:
 *   - out_info contains player, paddle, gun, specials, eyedude,
 *     and per-ball state
 *   - out_level (if non-NULL) contains the block grid with
 *     occupied/type/counter_slide/random/hit_points and BLACK_BLK
 *     next_frame_offset (frame-relative)
 *
 * The schema version is emitted as a JSON literal by savegame_io
 * on write; savegame_data_t itself carries no version field.
 */
void savegame_system_capture(const game_ctx_t *ctx, savegame_data_t *out_info,
                             savegame_level_t *out_level);

/*
 * Restore the game context from the two savegame structs.  Pure:
 * mutates ctx via subsystem APIs, no file I/O.
 *
 * `info` must be non-NULL.  `level` may be NULL when the caller
 * doesn't have a block grid snapshot (autosave-then-load path); in
 * that case the block grid is left untouched.  The caller is
 * responsible for reloading the canonical level file when needed.
 */
void savegame_system_restore(game_ctx_t *ctx, const savegame_data_t *info,
                             const savegame_level_t *level);

/*
 * Save the full game state to disk (save-info.dat + save-level.dat).
 * Posts a user-visible message on success or failure.
 *
 * Returns 1 on success, 0 on failure.
 */
int savegame_system_save(game_ctx_t *ctx);

/*
 * Load the full game state from disk.  Reads save-info.dat first;
 * if save-level.dat is missing or unreadable, falls back to the
 * canonical level file (matches the spec's auto-save scheme where
 * level files are absent after a bonus-screen autosave).
 *
 * Posts a user-visible message on success or failure.
 * Returns 1 on success, 0 on failure.
 */
int savegame_system_load(game_ctx_t *ctx);

/*
 * Auto-save: writes save-info.dat only and deletes any stale
 * save-level.dat.  Called from the bonus screen after a level is
 * cleared (writing the cleared grid would trigger immediate level
 * completion on load).  No user message.
 *
 * Returns 1 on success, 0 on failure.
 */
int savegame_system_autosave(game_ctx_t *ctx);

#endif /* SAVEGAME_SYSTEM_H */
