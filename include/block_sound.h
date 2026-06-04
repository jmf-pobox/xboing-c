#ifndef BLOCK_SOUND_H
#define BLOCK_SOUND_H

/*
 * block_sound.h — pure mapping from block_type to sound name.
 *
 * Extracted from game_callbacks.c so the table can be tested
 * exhaustively without an audio context.  Mirrors
 * original/blocks.c:762 PlaySoundForBlock.
 *
 * Returns the sound asset name (e.g., "bomb") for a destroying hit on
 * the given block type, or NULL when the block type has no sound.
 * The returned pointer is to a string literal — caller does not free.
 */
const char *block_sound_name(int block_type);

#endif /* BLOCK_SOUND_H */
