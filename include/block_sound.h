#ifndef BLOCK_SOUND_H
#define BLOCK_SOUND_H

/*
 * block_sound.h — pure mapping from block_type to (sound name, volume).
 *
 * Extracted from game_callbacks.c so the table can be tested exhaustively
 * without an audio context.  Mirrors original/blocks.c:762 PlaySoundForBlock,
 * preserving the per-call volume the original passed to playSoundFile().
 *
 * Returned name is the sound asset key (e.g., "bomb"); volume is the
 * percent-of-master 0-100 to play it at, matching the original's Sun
 * backend semantics (see ADR-045 follow-up: per-call volume modulation
 * audit, docs/audits/2026-06-28-audio-volume-modulation.md).
 *
 * name == NULL means the block type has no sound — volume is undefined
 * in that case and callers must not emit anything.  The returned pointer
 * is to a string literal — caller does not free.
 */
typedef struct
{
    const char *name;
    int volume;
} block_sound_t;

block_sound_t block_sound_lookup(int block_type);

#endif /* BLOCK_SOUND_H */
