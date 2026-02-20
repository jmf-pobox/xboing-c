#ifndef BLOCK_TYPES_H
#define BLOCK_TYPES_H

/*
 * Block type integer constants and grid dimensions.
 *
 * Extracted from blocks.h so that pure-logic modules (score_logic.c,
 * ball_math.c) can reference block types without pulling in X11 headers.
 *
 * ALWAYS change the SetupBlockInfo() function in blocks.c as well if you
 * change these defines.
 */

#define NONE_BLK		-2
#define KILL_BLK		-1

#define RED_BLK			0
#define BLUE_BLK		1
#define GREEN_BLK		2
#define TAN_BLK			3
#define YELLOW_BLK		4
#define PURPLE_BLK		5
#define BULLET_BLK		6
#define BLACK_BLK		7
#define COUNTER_BLK		8
#define BOMB_BLK		9
#define DEATH_BLK		10
#define REVERSE_BLK		11
#define HYPERSPACE_BLK	12
#define EXTRABALL_BLK	13
#define MGUN_BLK		14
#define WALLOFF_BLK		15
#define MULTIBALL_BLK	16
#define STICKY_BLK		17
#define PAD_SHRINK_BLK	18
#define PAD_EXPAND_BLK	19
#define DROP_BLK		20
#define MAXAMMO_BLK		21
#define ROAMER_BLK		22
#define TIMER_BLK		23
#define RANDOM_BLK		24

#define DYNAMITE_BLK	25
#define BONUSX2_BLK		26
#define BONUSX4_BLK		27
#define BONUS_BLK		28
#define BLACKHIT_BLK	29

#define MAX_STATIC_BLOCKS 	25
#define MAX_BLOCKS 			30

#define MAX_ROW			18
#define MAX_COL			9

#endif /* BLOCK_TYPES_H */
