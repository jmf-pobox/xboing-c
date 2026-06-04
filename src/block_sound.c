#include "block_sound.h"

#include <stddef.h>

#include "block_types.h"

const char *block_sound_name(int block_type)
{
    switch (block_type)
    {
        case BOMB_BLK:
            return "bomb";
        case BULLET_BLK:
        case MAXAMMO_BLK:
            return "ammo";
        case RED_BLK:
        case GREEN_BLK:
        case BLUE_BLK:
        case TAN_BLK:
        case PURPLE_BLK:
        case YELLOW_BLK:
        case COUNTER_BLK:
        case RANDOM_BLK:
        case DROP_BLK:
            return "touch";
        case ROAMER_BLK:
            return "ouch";
        case EXTRABALL_BLK:
            return "ddloo";
        case MGUN_BLK:
            return "mgun";
        case WALLOFF_BLK:
            return "wallsoff";
        case BONUSX2_BLK:
        case BONUSX4_BLK:
        case BONUS_BLK:
            return "gate";
        case REVERSE_BLK:
            return "warp";
        case PAD_SHRINK_BLK:
            return "wzzz2";
        case PAD_EXPAND_BLK:
            return "wzzz";
        case MULTIBALL_BLK:
            return "spring";
        case TIMER_BLK:
            return "bonus";
        case STICKY_BLK:
            return "sticky";
        case DEATH_BLK:
            return "evillaugh";
        case BLACK_BLK:
            return "metal";
        case HYPERSPACE_BLK:
            return "hypspc";
        case DYNAMITE_BLK:
            /* DYNAMITE_BLK has no entry in original/blocks.c:762
             * PlaySoundForBlock — passing it triggered ErrorMessage().
             * Mark explicitly silent so the gap can't be inherited by
             * a new block type via default-fallthrough. */
            return NULL;
        case BLACKHIT_BLK:
            /* BLACKHIT_BLK is the render state after BLACK's first
             * cooldown hit, not a destructible type.  The destroying
             * hit comes in as BLACK_BLK. */
            return NULL;
        case NONE_BLK:
        case KILL_BLK:
        default:
            return NULL;
    }
}
