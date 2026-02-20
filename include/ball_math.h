#ifndef BALL_MATH_H
#define BALL_MATH_H

/*
 * Pure ball physics functions — no X11 dependency.
 *
 * Extracted from ball.c for testability. Each function takes all inputs
 * as parameters and produces outputs via return values or out-parameters.
 * No global state is read or written.
 */

#include "ball_types.h"

/*
 * Initialize the machine epsilon constant used in collision detection.
 * Must be called once before ball_math_will_collide().
 * Returns the computed MACHINE_EPS value.
 */
float ball_math_init(void);

/*
 * Swept-circle collision detection between two balls.
 *
 * Returns 1 (True) if ball1 and ball2 will collide within the current
 * timestep, and writes the collision time to *time.
 * Returns 0 (False) if they will not collide.
 *
 * machine_eps: the value returned by ball_math_init().
 */
int ball_math_will_collide(const BALL *ball1, const BALL *ball2, float *time, float machine_eps);

/*
 * Compute new velocities after ball-ball elastic collision.
 *
 * Modifies ball1->dx, ball1->dy, ball2->dx, ball2->dy in place.
 * Position fields are read but not modified.
 *
 * NOTE: Preserves the known bug from ball.c:1744 where p.y uses
 * ball1->ballx instead of ball1->bally. This is a characterization
 * extraction — do not fix.
 */
void ball_math_collide(BALL *ball1, BALL *ball2);

/*
 * Compute paddle bounce reflection angles.
 *
 * Given the ball's current velocity (vx, vy), the hit position relative
 * to paddle center, the paddle size (including ball width compensation),
 * and the paddle's horizontal velocity, compute new ball dx and dy.
 *
 * The trig is: alpha = atan(Vx / -Vy), beta = atan(hit_pos / pad_size),
 * gamma = 2*beta - alpha, then Vx = Vs*sin(gamma), Vy = -Vs*cos(gamma).
 *
 * Ensures dy <= -MIN_DY_BALL (ball always moves upward after paddle hit).
 */
void ball_math_paddle_bounce(int vx, int vy, int hit_pos, int pad_size, int paddle_dx, int *new_dx,
                             int *new_dy);

/*
 * Normalize ball speed to match the given speed level.
 *
 * Scales dx and dy so the ball's total speed magnitude matches:
 *   target = sqrt(MAX_X_VEL^2 + MAX_Y_VEL^2) / 9.0 * speed_level
 *
 * Ensures neither dx nor dy is zero (minimum MIN_DX_BALL / MIN_DY_BALL).
 */
void ball_math_normalize_speed(int *dx, int *dy, int speed_level);

/*
 * Coordinate-to-grid-column conversion.
 * Equivalent to the X2COL macro: col = x / col_width.
 */
int ball_math_x_to_col(int x, int col_width);

/*
 * Coordinate-to-grid-row conversion.
 * Equivalent to the Y2ROW macro: row = y / row_height.
 */
int ball_math_y_to_row(int y, int row_height);

#endif /* BALL_MATH_H */
