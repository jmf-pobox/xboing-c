#ifndef BALL_TYPES_H
#define BALL_TYPES_H

/*
 * Ball data types and constants — no X11 dependency.
 *
 * Extracted from ball.h so that pure-logic modules (ball_math.c, tests)
 * can work with ball data without pulling in Xlib.
 */

/*
 *  Constants and macros:
 */

#define BALL_WIDTH  		20
#define BALL_HEIGHT 		19
#define MAX_BALL_MASS 		3.0
#define MIN_BALL_MASS 		1.0

#define BALL_WC     		(BALL_WIDTH / 2)
#define BALL_HC     		(BALL_HEIGHT / 2)

#define BIRTH_SLIDES        8
#define BALL_SLIDES         5

#define MAX_BALLS 			5

#define MAX_X_VEL           14
#define MAX_Y_VEL           14

#define MIN_DY_BALL			2
#define MIN_DX_BALL			2

#define BALL_ANIM_RATE      50
#define BIRTH_FRAME_RATE    5
#define BALL_FRAME_RATE     5
#define BORDER_ANIM_DELAY   15

#define PADDLE_HIT_SCORE    10

#define BALL_AUTO_ACTIVE_DELAY  3000

#define DIST_BALL_OF_PADDLE 45

#define PADDLE_BALL_FRAME_TILT  5000

/*
 *  Type declarations:
 */

enum BallStates
{
	BALL_POP,
	BALL_ACTIVE,
	BALL_STOP,
	BALL_CREATE,
	BALL_DIE,
	BALL_WAIT,
	BALL_READY,
	BALL_NONE
};

typedef struct ball
{
	enum BallStates	waitMode;		/* Ball waiting mode */
	int				waitingFrame;	/* Frame to wait until */
	int				newMode;		/* Ball's new mode */
	int				nextFrame;		/* next frame for something */
	int				active;			/* True - in use, False - dead */
    int             oldx;			/* Old x coord of ball centre */
    int             oldy;			/* Old y coord of ball centre */
    int             ballx;			/* Current x coord of ball centre */
    int             bally;			/* Current y coord of ball centre */
    int             dx;				/* Change in x axis increment */
    int             dy;				/* Change in y axis increment */
    int             slide;			/* Current pixmap visible */
	float          	radius;			/* The radius of the ball */
	float          	mass;			/* The mass of the ball */
	int 			lastPaddleHitFrame;	/* Last frame the ball hit paddle */
    enum BallStates	ballState;		/* The state of the ball */
} BALL;

#endif /* BALL_TYPES_H */
