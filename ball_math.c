/*
 * ball_math.c — Pure ball physics functions, no X11 dependency.
 *
 * Extracted from ball.c for testability. All functions are pure: inputs
 * come from parameters, outputs go to return values or out-parameters.
 *
 * CHARACTERIZATION EXTRACTION: This code replicates the exact behavior
 * of ball.c, including known bugs. Do not fix bugs here — document them
 * and file separate beads.
 */

#include <math.h>
#include "ball_math.h"

/* SQR returns the square of x */
#ifndef SQR
#define SQR(x) ((x)*(x))
#endif

#ifndef MIN
#define MIN(a,b) ((a)<(b) ? (a):(b))
#endif

/*
 * MINFLOAT: the smallest positive float.
 * ball.c uses <values.h> which is a non-standard BSD header.
 * We define it inline for portability.
 */
#ifndef MINFLOAT
#define MINFLOAT ((float)1.40129846432481707e-45)
#endif

float ball_math_init(void)
{
    return (float)sqrt((double)MINFLOAT);
}

int ball_math_will_collide(const BALL *ball1, const BALL *ball2,
                           float *time, float machine_eps)
{
    /*
     * Calculate when 2 balls will collide.
     * Replica of ball.c WhenBallsCollide() (line 1656).
     */

    float px, py, vx, vy;
    float tmp1, tmp2, t1, t2, tmin, v2, r2;

    px = (float)(ball1->ballx - ball2->ballx);
    py = (float)(ball1->bally - ball2->bally);
    vx = (float)(ball1->dx - ball2->dx);
    vy = (float)(ball1->dy - ball2->dy);

    v2 = SQR(vx) + SQR(vy);
    r2 = SQR(ball1->radius + ball2->radius);

    tmp2 = (v2 * r2) - SQR((vx * py) - (vy * px));

    if (tmp2 >= 0.0f && v2 > machine_eps)
    {
        tmp2 = (float)(sqrt((double)tmp2) / (double)v2);
        tmp1 = -((px * vx) + (py * vy)) / v2;

        t1 = tmp1 - tmp2;
        t2 = tmp1 + tmp2;

        tmin = MIN(t1, t2);

        if (tmin >= 0.0f && tmin <= 1.0f)
        {
            *time = tmin;
            return 1;
        }
    }

    *time = 0.0f;
    return 0;
}

void ball_math_collide(BALL *ball1, BALL *ball2)
{
    /*
     * Compute new velocities after ball-ball collision.
     * Replica of ball.c Ball2BallCollision() (line 1726).
     *
     * KNOWN BUG (ball.c:1744): p.y uses ball1->ballx instead of
     * ball1->bally. This is preserved here for characterization.
     */

    float px, py, vx, vy;
    float k, plen, massrate;

    px = (float)(ball1->ballx - ball2->ballx);
    py = (float)(ball1->ballx - ball2->bally);  /* BUG: ballx not bally */
    vx = (float)(ball1->dx - ball2->dx);
    vy = (float)(ball1->dy - ball2->dy);

    plen = (float)sqrt((double)(SQR(px) + SQR(py)));
    px /= plen;
    py /= plen;

    massrate = ball1->mass / ball2->mass;

    k = -2.0f * ((vx * px) + (vy * py)) / (1.0f + massrate);
    ball1->dx += (int)(k * px);
    ball1->dy += (int)(k * py);

    k *= -massrate;
    ball2->dx += (int)(k * px);
    ball2->dy += (int)(k * py);
}

void ball_math_paddle_bounce(int vx, int vy, int hit_pos, int pad_size,
                             int paddle_dx, int *new_dx, int *new_dy)
{
    /*
     * Compute paddle bounce reflection.
     * Replica of ball.c UpdateABall() lines 1238-1273.
     */

    float Vx, Vy, Vs, alpha, beta, gamma;

    Vx = (float)vx;
    Vy = (float)vy;

    /* speed intensity of the ball */
    Vs = (float)sqrt((double)(Vx * Vx + Vy * Vy));

    alpha = (float)atan((double)(Vx / -Vy));

    Vx = (float)hit_pos;
    Vy = (float)pad_size / 1.0f;

    beta = (float)atan((double)(Vx / Vy));
    gamma = 2.0f * beta - alpha;

    Vx = Vs * (float)sin((double)gamma);
    Vy = -Vs * (float)cos((double)gamma);

    /* take in account the horizontal speed of the paddle */
    Vx += (float)(paddle_dx / 10.0);

    if (Vx > 0.0f)
        *new_dx = (int)(Vx + 0.5f);
    else
        *new_dx = (int)(Vx - 0.5f);

    if (Vy < 0.0f)
        *new_dy = (int)(Vy - 0.5f);
    else
        *new_dy = -MIN_DY_BALL;

    if (*new_dy > -MIN_DY_BALL)
        *new_dy = -MIN_DY_BALL;
}

void ball_math_normalize_speed(int *dx, int *dy, int speed_level)
{
    /*
     * Normalize ball speed to match the given speed level.
     * Replica of ball.c UpdateABall() lines 1305-1333.
     */

    float Vx, Vy, Vs, alpha, beta;

    Vx = (float)*dx;
    Vy = (float)*dy;
    Vs = (float)sqrt((double)(Vx * Vx + Vy * Vy));

    alpha = (float)sqrt((double)((float)MAX_X_VEL * (float)MAX_X_VEL +
                                  (float)MAX_Y_VEL * (float)MAX_Y_VEL));
    alpha /= 9.0f; /* number of speed levels */
    alpha *= (float)speed_level;

    if (Vs == 0.0f) Vs = 1.0f;
    beta = alpha / Vs;

    Vx *= beta;
    Vy *= beta;

    if (Vx > 0.0f)
        *dx = (int)(Vx + 0.5f);
    else
        *dx = (int)(Vx - 0.5f);

    if (Vy > 0.0f)
        *dy = (int)(Vy + 0.5f);
    else
        *dy = (int)(Vy - 0.5f);

    if (*dy == 0)
        *dy = MIN_DY_BALL;

    if (*dx == 0)
        *dx = MIN_DX_BALL;
}

int ball_math_x_to_col(int x, int col_width)
{
    return x / col_width;
}

int ball_math_y_to_row(int y, int row_height)
{
    return y / row_height;
}
