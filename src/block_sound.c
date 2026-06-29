#include "block_sound.h"

#include <stddef.h>

#include "block_types.h"

block_sound_t block_sound_lookup(int block_type)
{
    switch (block_type)
    {
        case BOMB_BLK:
            return (block_sound_t){"bomb", 50};
        case BULLET_BLK:
            return (block_sound_t){"ammo", 30};
        case MAXAMMO_BLK:
            return (block_sound_t){"ammo", 70};
        case RED_BLK:
        case GREEN_BLK:
        case BLUE_BLK:
        case TAN_BLK:
        case PURPLE_BLK:
        case YELLOW_BLK:
        case COUNTER_BLK:
        case RANDOM_BLK:
        case DROP_BLK:
            return (block_sound_t){"touch", 99};
        case ROAMER_BLK:
            return (block_sound_t){"ouch", 99};
        case EXTRABALL_BLK:
            return (block_sound_t){"ddloo", 99};
        case MGUN_BLK:
            return (block_sound_t){"mgun", 99};
        case WALLOFF_BLK:
            return (block_sound_t){"wallsoff", 99};
        case BONUSX2_BLK:
        case BONUSX4_BLK:
        case BONUS_BLK:
            return (block_sound_t){"gate", 99};
        case REVERSE_BLK:
            return (block_sound_t){"warp", 99};
        case PAD_SHRINK_BLK:
            return (block_sound_t){"wzzz2", 99};
        case PAD_EXPAND_BLK:
            return (block_sound_t){"wzzz", 99};
        case MULTIBALL_BLK:
            return (block_sound_t){"spring", 80};
        case TIMER_BLK:
            return (block_sound_t){"bonus", 50};
        case STICKY_BLK:
            return (block_sound_t){"sticky", 90};
        case DEATH_BLK:
            return (block_sound_t){"evillaugh", 99};
        case BLACK_BLK:
            return (block_sound_t){"metal", 99};
        case HYPERSPACE_BLK:
            return (block_sound_t){"hypspc", 99};
        case DYNAMITE_BLK:
            /* DYNAMITE_BLK has no entry in original/blocks.c:762
             * PlaySoundForBlock — passing it triggered ErrorMessage().
             * Mark explicitly silent so the gap can't be inherited by
             * a new block type via default-fallthrough. */
            return (block_sound_t){NULL, 0};
        case BLACKHIT_BLK:
            /* BLACKHIT_BLK is the render state after BLACK's first
             * cooldown hit, not a destructible type.  The destroying
             * hit comes in as BLACK_BLK. */
            return (block_sound_t){NULL, 0};
        case NONE_BLK:
        case KILL_BLK:
        default:
            return (block_sound_t){NULL, 0};
    }
}
