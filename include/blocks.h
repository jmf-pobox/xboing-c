#ifndef _BLOCKS_H_
#define _BLOCKS_H_

/*
 * XBoing - An X11 blockout style computer game
 *
 * (c) Copyright 1993, 1994, 1995, Justin C. Kibell, All Rights Reserved
 *
 * The X Consortium, and any party obtaining a copy of these files from
 * the X Consortium, directly or indirectly, is granted, free of charge, a
 * full and unrestricted irrevocable, world-wide, paid up, royalty-free,
 * nonexclusive right and license to deal in this software and
 * documentation files (the "Software"), including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons who receive
 * copies from any such party to do so.  This license includes without
 * limitation a license to do the foregoing actions under any patents of
 * the party supplying this software to the X Consortium.
 *
 * In no event shall the author be liable to any party for direct, indirect,
 * special, incidental, or consequential damages arising out of the use of
 * this software and its documentation, even if the author has been advised
 * of the possibility of such damage.
 *
 * The author specifically disclaims any warranties, including, but not limited
 * to, the implied warranties of merchantability and fitness for a particular
 * purpose.  The software provided hereunder is on an "AS IS" basis, and the
 * author has no obligation to provide maintenance, support, updates,
 * enhancements, or modifications.
 */

/* 
 * =========================================================================
 *
 * $Id: blocks.h,v 1.1.1.1 1994/12/16 01:36:50 jck Exp $
 * $Source: /usr5/legends/jck/xb/master/xboing/include/blocks.h,v $
 * $Revision: 1.1.1.1 $
 * $Date: 1994/12/16 01:36:50 $
 *
 * $Log: blocks.h,v $
 * Revision 1.1.1.1  1994/12/16  01:36:50  jck
 * The XBoing distribution requires configuration management. This is why the
 * cvs utility is being used. This is the initial import of all source etc..
 *
 *
 * =========================================================================
 */

/*
 *  Dependencies on other include files:
 */

#include <X11/Xlib.h>
#include "block_types.h"

/*
 *  Constants and macros:
 */

#define BLOCK_WIDTH		40
#define BLOCK_HEIGHT	20

#define SPACE			7

#define REGION_NONE		0
#define REGION_TOP		1
#define REGION_BOTTOM	2
#define REGION_LEFT		4
#define REGION_RIGHT	8

#define EXPLODE_DELAY               10
#define BONUS_DELAY                 150
#define BONUS_LENGTH                1500
#define NUMBER_OF_BULLETS_NEW_LEVEL 4
#define DEATH_DELAY1                100
#define DEATH_DELAY2                700
#define EXTRABALL_DELAY             300
#define RANDOM_DELAY                500
#define DROP_DELAY                  1000
#define INFINITE_DELAY              9999999
#define ROAM_EYES_DELAY             300
#define ROAM_DELAY                  1000
#define EXTRA_TIME                  20


/*
 *  Type declarations:
 */

struct blockInfo
{
	int 	blockType;
	int 	width;
	int 	height;
	int		slide;
};

struct aBlock
{
	/* General properties of the block */
	int     	occupied;
	int         blockType;
	int 		hitPoints;

	/* Used when block explodes */
	int     	exploding;
	int 		explodeStartFrame;
	int 		explodeNextFrame;
	int 		explodeSlide;

	/* Used for animation of object */
	int     	currentFrame;
	int     	nextFrame;
	int     	lastFrame;

	/* Used for positioning of block in arena */
	int			blockOffsetX;
	int			blockOffsetY;
	int			x;
	int			y;
	int         width;
	int         height;

	/* Used for ball collision with block */
	Region		regionTop;
	Region		regionBottom;
	Region		regionLeft;
	Region		regionRight;

	/* Indexes into animation frames for object */
	int 		counterSlide;		/* For counter blocks only */
	int 		bonusSlide;			/* For bonus blocks only */

	/* Special types of block flags */
	int			random;
	int			drop;
	int     	specialPopup;
	int 		explodeAll;

	/* Used for splitting of the ball in multiball mode */
	int 		ballHitIndex;
	int			balldx;
	int			balldy;
};

typedef struct aBlock **BLOCKPTR;

/*
 *  Function prototypes:
 */

#if NeedFunctionPrototypes
void FreeBlockPixmaps(Display *display);
void InitialiseBlocks(Display *display, Window window, Colormap colormap);
void DrawBlock(Display *display, Window window, int row, int col, 
	int blockType);
void ExplodeBlocksPending(Display *display, Window window);
void RedrawAllBlocks(Display *display, Window window);
void DrawTheBlock(Display *display, Window window, int x, int y, 
	int blockType, int slide, int r, int c);
void ExplodeBlockType(Display *display, Window window, int x, int y,
	int row, int col, int type, int slide);
void AddNewBlock(Display *display, Window window, int row, int col,
	int blockType, int counterSlide, int drawIt);
void HandlePendingAnimations(Display *display, Window window);
void AddBonusBlock(Display *display, Window window, int *row, int *col,
	int type);
void ClearBlockArray(void);
int StillActiveBlocks(void);
void SkipToNextLevel(Display *display, Window window);
void PlaySoundForBlock(int type);
void AddSpecialBlock(Display *display, Window window, int *row, int *col,
	int type, int kill_shots);
void HandlePendingSpecials(Display *display, Window window, int type,
	int r, int c);
int GetRandomType(int blankBlock);
void SetExplodeAllType(Display *display, Window window, int type);
void EraseVisibleBlock(Display *display, Window window, int row, int col);
void ClearBlock(int row, int col);
void SetupBlockInfo(void);
#else
void SetupBlockInfo();
void ClearBlock();
void EraseVisibleBlock();
void SetExplodeAllType();
int GetRandomType();
void HandlePendingSpecials();
void AddSpecialBlock();
void PlaySoundForBlock();
void FreeBlockPixmaps();
void InitialiseBlocks();
void DrawBlock();
void ExplodeBlocksPending();
void RedrawAllBlocks();
void DrawTheBlock();
void ExplodeBlockType();
void AddNewBlock();
void HandlePendingAnimations();
void AddBonusBlock();
void ClearBlockArray();
int StillActiveBlocks();
void SkipToNextLevel();
#endif

extern struct aBlock blocks[MAX_ROW][MAX_COL];
extern int rowHeight;
extern int colWidth;
extern int blocksExploding;
extern Pixmap exyellowblock[3], exyellowblockM[3];
extern struct blockInfo    BlockInfo[MAX_BLOCKS];


#endif
