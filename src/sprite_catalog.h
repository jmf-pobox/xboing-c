/*
 * sprite_catalog.h -- Texture key constants for the SDL2 game.
 *
 * Maps game concepts (ball frame 2, red block, huge paddle) to
 * texture cache keys ("balls/ball2", "blocks/redblk", "paddle/padhuge").
 *
 * Keys match the file paths relative to assets/images/ with .png stripped,
 * as loaded by sdl2_texture_create().
 *
 * Grouping mirrors the asset directory structure.
 */

#ifndef SPRITE_CATALOG_H
#define SPRITE_CATALOG_H

/* =========================================================================
 * Balls  (assets/images/balls/)
 * ========================================================================= */

#define SPR_BALL_1 "balls/ball1"
#define SPR_BALL_2 "balls/ball2"
#define SPR_BALL_3 "balls/ball3"
#define SPR_BALL_4 "balls/ball4"
#define SPR_BALL_KILLER "balls/killer"
#define SPR_BALL_LIFE "balls/life"

/* Birth animation frames (1-8) */
#define SPR_BALL_BIRTH_1 "balls/bbirth1"
#define SPR_BALL_BIRTH_2 "balls/bbirth2"
#define SPR_BALL_BIRTH_3 "balls/bbirth3"
#define SPR_BALL_BIRTH_4 "balls/bbirth4"
#define SPR_BALL_BIRTH_5 "balls/bbirth5"
#define SPR_BALL_BIRTH_6 "balls/bbirth6"
#define SPR_BALL_BIRTH_7 "balls/bbirth7"
#define SPR_BALL_BIRTH_8 "balls/bbirth8"

/* =========================================================================
 * Paddle  (assets/images/paddle/)
 * ========================================================================= */

#define SPR_PADDLE_SMALL "paddle/padsml"
#define SPR_PADDLE_MEDIUM "paddle/padmed"
#define SPR_PADDLE_HUGE "paddle/padhuge"

/* =========================================================================
 * Blocks -- standard colors  (assets/images/blocks/)
 * ========================================================================= */

#define SPR_BLOCK_RED "blocks/redblk"
#define SPR_BLOCK_BLUE "blocks/blueblk"
#define SPR_BLOCK_GREEN "blocks/grnblk"
#define SPR_BLOCK_TAN "blocks/tanblk"
#define SPR_BLOCK_YELLOW "blocks/yellblk"
#define SPR_BLOCK_PURPLE "blocks/purpblk"
#define SPR_BLOCK_BLACK "blocks/blakblk"
#define SPR_BLOCK_BLACK_HIT "blocks/blakblkH"

/* =========================================================================
 * Blocks -- special types  (assets/images/blocks/)
 * ========================================================================= */

#define SPR_BLOCK_BOMB "blocks/bombblk"
#define SPR_BLOCK_COUNTER "blocks/cntblk"
#define SPR_BLOCK_COUNTER_1 "blocks/cntblk1"
#define SPR_BLOCK_COUNTER_2 "blocks/cntblk2"
#define SPR_BLOCK_COUNTER_3 "blocks/cntblk3"
#define SPR_BLOCK_COUNTER_4 "blocks/cntblk4"
#define SPR_BLOCK_COUNTER_5 "blocks/cntblk5"
#define SPR_BLOCK_DEATH_1 "blocks/death1"
#define SPR_BLOCK_DEATH_2 "blocks/death2"
#define SPR_BLOCK_DEATH_3 "blocks/death3"
#define SPR_BLOCK_DEATH_4 "blocks/death4"
#define SPR_BLOCK_DEATH_5 "blocks/death5"
#define SPR_BLOCK_DYNAMITE "blocks/dynamite"
#define SPR_BLOCK_HYPERSPACE "blocks/hypspc"
#define SPR_BLOCK_STICKY "blocks/stkyblk"
#define SPR_BLOCK_REVERSE "blocks/reverse"
#define SPR_BLOCK_WALLOFF "blocks/walloff"
#define SPR_BLOCK_MULTIBALL "blocks/multibal"
#define SPR_BLOCK_MACHGUN "blocks/machgun"
#define SPR_BLOCK_LOTSAMMO "blocks/lotsammo"
#define SPR_BLOCK_PAD_EXPAND "blocks/padexpn"
#define SPR_BLOCK_PAD_SHRINK "blocks/padshrk"
#define SPR_BLOCK_EXTRABALL "blocks/xtrabal"
#define SPR_BLOCK_EXTRABALL_2 "blocks/xtrabal2"

/* Bonus blocks (x2, x4, generic bonus) */
#define SPR_BLOCK_X2_1 "blocks/x2bonus1"
#define SPR_BLOCK_X2_2 "blocks/x2bonus2"
#define SPR_BLOCK_X2_3 "blocks/x2bonus3"
#define SPR_BLOCK_X2_4 "blocks/x2bonus4"
#define SPR_BLOCK_X4_1 "blocks/x4bonus1"
#define SPR_BLOCK_X4_2 "blocks/x4bonus2"
#define SPR_BLOCK_X4_3 "blocks/x4bonus3"
#define SPR_BLOCK_X4_4 "blocks/x4bonus4"
#define SPR_BLOCK_BONUS_1 "blocks/bonus1"
#define SPR_BLOCK_BONUS_2 "blocks/bonus2"
#define SPR_BLOCK_BONUS_3 "blocks/bonus3"
#define SPR_BLOCK_BONUS_4 "blocks/bonus4"

/* Roamer / drop */
#define SPR_BLOCK_ROAMER "blocks/roamer"
#define SPR_BLOCK_ROAMER_L "blocks/roamerL"
#define SPR_BLOCK_ROAMER_R "blocks/roamerR"
#define SPR_BLOCK_ROAMER_U "blocks/roamerU"
#define SPR_BLOCK_ROAMER_D "blocks/roamerD"
#define SPR_BLOCK_CLOCK "blocks/clock"

/* =========================================================================
 * Block explosions  (assets/images/blockex/)
 * ========================================================================= */

#define SPR_EXPLODE_RED_1 "blockex/exred1"
#define SPR_EXPLODE_RED_2 "blockex/exred2"
#define SPR_EXPLODE_RED_3 "blockex/exred3"
#define SPR_EXPLODE_BLUE_1 "blockex/exblue1"
#define SPR_EXPLODE_BLUE_2 "blockex/exblue2"
#define SPR_EXPLODE_BLUE_3 "blockex/exblue3"
#define SPR_EXPLODE_GREEN_1 "blockex/exgren1"
#define SPR_EXPLODE_GREEN_2 "blockex/exgren2"
#define SPR_EXPLODE_GREEN_3 "blockex/exgren3"
#define SPR_EXPLODE_TAN_1 "blockex/extan1"
#define SPR_EXPLODE_TAN_2 "blockex/extan2"
#define SPR_EXPLODE_TAN_3 "blockex/extan3"
#define SPR_EXPLODE_YELLOW_1 "blockex/exyell1"
#define SPR_EXPLODE_YELLOW_2 "blockex/exyell2"
#define SPR_EXPLODE_YELLOW_3 "blockex/exyell3"
#define SPR_EXPLODE_PURPLE_1 "blockex/expurp1"
#define SPR_EXPLODE_PURPLE_2 "blockex/expurp2"
#define SPR_EXPLODE_PURPLE_3 "blockex/expurp3"
#define SPR_EXPLODE_BOMB_1 "blockex/exbomb1"
#define SPR_EXPLODE_BOMB_2 "blockex/exbomb2"
#define SPR_EXPLODE_BOMB_3 "blockex/exbomb3"
#define SPR_EXPLODE_COUNTER_1 "blockex/excnt1"
#define SPR_EXPLODE_COUNTER_2 "blockex/excnt2"
#define SPR_EXPLODE_COUNTER_3 "blockex/excnt3"
#define SPR_EXPLODE_DEATH_1 "blockex/exdeath1"
#define SPR_EXPLODE_DEATH_2 "blockex/exdeath2"
#define SPR_EXPLODE_DEATH_3 "blockex/exdeath3"
#define SPR_EXPLODE_DEATH_4 "blockex/exdeath4"
#define SPR_EXPLODE_X2_1 "blockex/exx2bs1"
#define SPR_EXPLODE_X2_2 "blockex/exx2bs2"
#define SPR_EXPLODE_X2_3 "blockex/exx2bs3"

/* =========================================================================
 * Guns  (assets/images/guns/)
 * ========================================================================= */

#define SPR_BULLET "guns/bullet"
#define SPR_TINK "guns/tink"

/* =========================================================================
 * Digits  (assets/images/digits/)
 * ========================================================================= */

#define SPR_DIGIT_0 "digits/digit0"
#define SPR_DIGIT_1 "digits/digit1"
#define SPR_DIGIT_2 "digits/digit2"
#define SPR_DIGIT_3 "digits/digit3"
#define SPR_DIGIT_4 "digits/digit4"
#define SPR_DIGIT_5 "digits/digit5"
#define SPR_DIGIT_6 "digits/digit6"
#define SPR_DIGIT_7 "digits/digit7"
#define SPR_DIGIT_8 "digits/digit8"
#define SPR_DIGIT_9 "digits/digit9"

/* =========================================================================
 * Stars / sparkle  (assets/images/stars/)
 * ========================================================================= */

#define SPR_STAR_1 "stars/star1"
#define SPR_STAR_2 "stars/star2"
#define SPR_STAR_3 "stars/star3"
#define SPR_STAR_4 "stars/star4"
#define SPR_STAR_5 "stars/star5"
#define SPR_STAR_6 "stars/star6"
#define SPR_STAR_7 "stars/star7"
#define SPR_STAR_8 "stars/star8"
#define SPR_STAR_9 "stars/star9"
#define SPR_STAR_10 "stars/star10"
#define SPR_STAR_11 "stars/star11"

/* =========================================================================
 * Backgrounds  (assets/images/bgrnds/)
 * ========================================================================= */

#define SPR_BGRND "bgrnds/bgrnd"
#define SPR_BGRND_2 "bgrnds/bgrnd2"
#define SPR_BGRND_3 "bgrnds/bgrnd3"
#define SPR_BGRND_4 "bgrnds/bgrnd4"
#define SPR_BGRND_5 "bgrnds/bgrnd5"
#define SPR_BGRND_MAIN "bgrnds/mnbgrnd"
#define SPR_BGRND_SPACE "bgrnds/space"

/* =========================================================================
 * Guides  (assets/images/guides/)
 * ========================================================================= */

#define SPR_GUIDE "guides/guide"
#define SPR_GUIDE_1 "guides/guide1"
#define SPR_GUIDE_2 "guides/guide2"
#define SPR_GUIDE_3 "guides/guide3"
#define SPR_GUIDE_4 "guides/guide4"
#define SPR_GUIDE_5 "guides/guide5"
#define SPR_GUIDE_6 "guides/guide6"
#define SPR_GUIDE_7 "guides/guide7"
#define SPR_GUIDE_8 "guides/guide8"
#define SPR_GUIDE_9 "guides/guide9"
#define SPR_GUIDE_10 "guides/guide10"
#define SPR_GUIDE_11 "guides/guide11"

/* =========================================================================
 * EyeDude  (assets/images/eyes/)
 * ========================================================================= */

#define SPR_EYEDUDE "eyes/deveyes"
#define SPR_EYEDUDE_1 "eyes/deveyes1"
#define SPR_EYEDUDE_2 "eyes/deveyes2"
#define SPR_EYEDUDE_3 "eyes/deveyes3"
#define SPR_EYEDUDE_4 "eyes/deveyes4"
#define SPR_EYEDUDE_5 "eyes/deveyes5"
#define SPR_EYEDUDE_DEAD "eyes/guydead"
#define SPR_GUY_LEFT_1 "eyes/guyl1"
#define SPR_GUY_LEFT_2 "eyes/guyl2"
#define SPR_GUY_LEFT_3 "eyes/guyl3"
#define SPR_GUY_LEFT_4 "eyes/guyl4"
#define SPR_GUY_LEFT_5 "eyes/guyl5"
#define SPR_GUY_LEFT_6 "eyes/guyl6"
#define SPR_GUY_RIGHT_1 "eyes/guyr1"
#define SPR_GUY_RIGHT_2 "eyes/guyr2"
#define SPR_GUY_RIGHT_3 "eyes/guyr3"
#define SPR_GUY_RIGHT_4 "eyes/guyr4"
#define SPR_GUY_RIGHT_5 "eyes/guyr5"
#define SPR_GUY_RIGHT_6 "eyes/guyr6"

/* =========================================================================
 * Presents / title screen  (assets/images/presents/)
 * ========================================================================= */

#define SPR_PRESENTS "presents/presents"
#define SPR_PRESENTS_EARTH "presents/earth"
#define SPR_PRESENTS_FLAG "presents/flag"
#define SPR_PRESENTS_JUSTIN "presents/justin"
#define SPR_PRESENTS_KIBELL "presents/kibell"
#define SPR_TITLE_BIG "presents/titleBig"
#define SPR_TITLE_SMALL "presents/titleSml"
#define SPR_TITLE_X "presents/titleX"
#define SPR_TITLE_B "presents/titleB"
#define SPR_TITLE_O "presents/titleO"
#define SPR_TITLE_I "presents/titleI"
#define SPR_TITLE_N "presents/titleN"
#define SPR_TITLE_G "presents/titleG"

/* =========================================================================
 * Miscellaneous  (assets/images/ root)
 * ========================================================================= */

#define SPR_FLOPPY "floppy"
#define SPR_HIGHSCORE "highscr"
#define SPR_ICON "icon"
#define SPR_LEFT_ARROW "larrow"
#define SPR_RIGHT_ARROW "rarrow"
#define SPR_MOUSE "mouse"
#define SPR_QUESTION "question"
#define SPR_TEXT "text"

/* =========================================================================
 * Lookup helpers -- map block_types.h constants to texture keys
 * ========================================================================= */

#include <stddef.h>

#include "block_types.h"

/*
 * Return the texture key for a block type constant.
 * Returns NULL for NONE_BLK, KILL_BLK, and unknown types.
 */
static inline const char *sprite_block_key(int block_type)
{
    switch (block_type)
    {
    case RED_BLK:
        return SPR_BLOCK_RED;
    case BLUE_BLK:
        return SPR_BLOCK_BLUE;
    case GREEN_BLK:
        return SPR_BLOCK_GREEN;
    case TAN_BLK:
        return SPR_BLOCK_TAN;
    case YELLOW_BLK:
        return SPR_BLOCK_YELLOW;
    case PURPLE_BLK:
        return SPR_BLOCK_PURPLE;
    case BULLET_BLK:
        return SPR_BLOCK_LOTSAMMO;
    case BLACK_BLK:
        return SPR_BLOCK_BLACK;
    case COUNTER_BLK:
        return SPR_BLOCK_COUNTER;
    case BOMB_BLK:
        return SPR_BLOCK_BOMB;
    case DEATH_BLK:
        return SPR_BLOCK_DEATH_1;
    case REVERSE_BLK:
        return SPR_BLOCK_REVERSE;
    case HYPERSPACE_BLK:
        return SPR_BLOCK_HYPERSPACE;
    case EXTRABALL_BLK:
        return SPR_BLOCK_EXTRABALL;
    case MGUN_BLK:
        return SPR_BLOCK_MACHGUN;
    case WALLOFF_BLK:
        return SPR_BLOCK_WALLOFF;
    case MULTIBALL_BLK:
        return SPR_BLOCK_MULTIBALL;
    case STICKY_BLK:
        return SPR_BLOCK_STICKY;
    case PAD_SHRINK_BLK:
        return SPR_BLOCK_PAD_SHRINK;
    case PAD_EXPAND_BLK:
        return SPR_BLOCK_PAD_EXPAND;
    case DROP_BLK:
        return SPR_BLOCK_RED; /* Drop uses row-dependent coloring */
    case MAXAMMO_BLK:
        return SPR_BLOCK_LOTSAMMO;
    case ROAMER_BLK:
        return SPR_BLOCK_ROAMER;
    case TIMER_BLK:
        return SPR_BLOCK_CLOCK;
    case RANDOM_BLK:
        return SPR_BLOCK_BONUS_1; /* Random uses animated bonus frames */
    case DYNAMITE_BLK:
        return SPR_BLOCK_DYNAMITE;
    case BONUSX2_BLK:
        return SPR_BLOCK_X2_1;
    case BONUSX4_BLK:
        return SPR_BLOCK_X4_1;
    case BONUS_BLK:
        return SPR_BLOCK_BONUS_1;
    case BLACKHIT_BLK:
        return SPR_BLOCK_BLACK_HIT;
    default:
        return NULL;
    }
}

/*
 * Return the texture key for a ball slide frame (0-4 cycles through 4 ball sprites).
 */
static inline const char *sprite_ball_key(int slide)
{
    switch (slide % 4)
    {
    case 0:
        return SPR_BALL_1;
    case 1:
        return SPR_BALL_2;
    case 2:
        return SPR_BALL_3;
    case 3:
        return SPR_BALL_4;
    default:
        return SPR_BALL_1;
    }
}

/*
 * Return the texture key for a ball birth animation frame (1-8).
 */
static inline const char *sprite_ball_birth_key(int frame)
{
    static const char *const frames[] = {
        SPR_BALL_BIRTH_1, SPR_BALL_BIRTH_2, SPR_BALL_BIRTH_3, SPR_BALL_BIRTH_4,
        SPR_BALL_BIRTH_5, SPR_BALL_BIRTH_6, SPR_BALL_BIRTH_7, SPR_BALL_BIRTH_8,
    };
    if (frame < 1 || frame > 8)
        return SPR_BALL_1;
    return frames[frame - 1];
}

/*
 * Return the texture key for a paddle size (40=small, 50=medium, 70=huge).
 */
static inline const char *sprite_paddle_key(int width)
{
    if (width <= 40)
        return SPR_PADDLE_SMALL;
    if (width <= 50)
        return SPR_PADDLE_MEDIUM;
    return SPR_PADDLE_HUGE;
}

/*
 * Return the texture key for a score digit (0-9).
 */
static inline const char *sprite_digit_key(int digit)
{
    static const char *const digits[] = {
        SPR_DIGIT_0, SPR_DIGIT_1, SPR_DIGIT_2, SPR_DIGIT_3, SPR_DIGIT_4,
        SPR_DIGIT_5, SPR_DIGIT_6, SPR_DIGIT_7, SPR_DIGIT_8, SPR_DIGIT_9,
    };
    if (digit < 0 || digit > 9)
        return SPR_DIGIT_0;
    return digits[digit];
}

/*
 * Return the texture key for a sparkle/star frame (1-11).
 */
static inline const char *sprite_star_key(int frame)
{
    static const char *const stars[] = {
        SPR_STAR_1,  SPR_STAR_2,  SPR_STAR_3, SPR_STAR_4, SPR_STAR_5, SPR_STAR_6,
        SPR_STAR_7,  SPR_STAR_8,  SPR_STAR_9, SPR_STAR_10, SPR_STAR_11,
    };
    if (frame < 1 || frame > 11)
        return SPR_STAR_1;
    return stars[frame - 1];
}

/*
 * Return the texture key for a background number (1-5).
 * Background 1 is the default; 2-5 cycle during gameplay.
 */
static inline const char *sprite_background_key(int number)
{
    switch (number)
    {
    case 1:
        return SPR_BGRND;
    case 2:
        return SPR_BGRND_2;
    case 3:
        return SPR_BGRND_3;
    case 4:
        return SPR_BGRND_4;
    case 5:
        return SPR_BGRND_5;
    default:
        return SPR_BGRND;
    }
}

#endif /* SPRITE_CATALOG_H */
