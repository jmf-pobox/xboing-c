#ifndef _BALL_H_
#define _BALL_H_

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
 * $Id: ball.h,v 1.1.1.1 1994/12/16 01:36:50 jck Exp $
 * $Source: /usr5/legends/jck/xb/master/xboing/include/ball.h,v $
 * $Revision: 1.1.1.1 $
 * $Date: 1994/12/16 01:36:50 $
 *
 * $Log: ball.h,v $
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
#include "ball_types.h"

/*
 *  Function prototypes:
 */

void InitialiseBall(Display *display, Window window, Colormap colormap);
void FreeBall(Display *display);
void RedrawBall(Display *display, Window window);
void DrawTheBall(Display *display, Window window, int x, int y, int slide);
void DrawTheBallBirth(Display *display, Window window, int x, int y, int slide);
void KillBallNow(Display *display, Window window, int i);
void GetBallPosition(int *ballX, int *ballY, int i);
void ResetBallStart(Display *display, Window window);
int GetBallMode(int i);
void ChangeBallMode(enum BallStates newMode, int i);
int AddANewBall(Display *display, int x, int y, int dx, int dy);
void ClearAllBalls(void);
void HandleBallMode(Display *display, Window window);
int StartAnotherBall(Display *display, Window window);
int IsBallWaiting(void);
void ClearBall(int i);
void SplitBallInTwo(Display *display, Window window);
void ClearBallNow(Display *display, Window window, int i);
int GetAnActiveBall(void);
int ActivateWaitingBall(Display *display, Window window);
int GetNumberOfActiveBalls(void);
void DoBoardTilt(Display *display, int i);

extern BALL balls[MAX_BALLS];
extern int speedLevel;
extern int paddleDx;

#endif
