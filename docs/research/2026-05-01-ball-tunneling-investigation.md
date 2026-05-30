# Ball Tunneling Investigation — xboing-c-895

**Date:** 2026-05-01
**Worker:** jdc (John D. Carmack)
**Ticket:** xboing-c-895
**Mission:** m-2026-05-02-009

## Symptom

Ball passed through 2 rows of blocks in level 1 without breaking them.
Observed during dogfood 2026-05-01.

---

## 1. Legacy Algorithm (original/ball.c)

### 1.1 Position update — original/ball.c:1037-1038

```c
balls[i].ballx = balls[i].oldx + balls[i].dx;
balls[i].bally = balls[i].oldy + balls[i].dy;
```

One-shot integer step: the ball moves `(dx, dy)` pixels per tick. No swept
guarantee at this stage.

### 1.2 Ray-march loop — original/ball.c:1210-1315

After the single-step position update, the legacy code converts the
**new** position to a grid cell, then ray-marches back from `(oldx, oldy)`
toward `(ballx, bally)` one pixel-step at a time:

```c
X2COL(col, balls[i].ballx);   /* ball.c:1210 */
Y2ROW(row, balls[i].bally);   /* ball.c:1211 */

x = balls[i].oldx;            /* ball.c:1213 */
y = balls[i].oldy;            /* ball.c:1214 */
/* ... compute incx, incy, step ... */
for (j = 0; j < step; j++) {  /* ball.c:1238 */
    if (CheckForCollision(..., x, y, &row, &col, i) != REGION_NONE) {
        /* bounce and break */
    }
    x += incx;
    y += incy;
}
```

`step = max(|dx|, |dy|)`. So for `dx=5, dy=-3`, step=5, and the loop
samples the trajectory at 5 sub-pixel positions. This is a discrete
ray-march — not a true continuous swept-collision — but it guarantees
at least one sample per pixel along the dominant axis. For normal game
speeds (max 14 px/tick per `MAX_X_VEL`/`MAX_Y_VEL`), the march visits
every pixel column (or row) on the path, making tunneling through a
20px-tall block geometrically impossible at those speeds.

**Anti-tunneling guarantee:** The legacy code is safe as long as
`step >= block_height` is never true in a single tick. Since
`MAX_X_VEL = MAX_Y_VEL = 14` and `BLOCK_HEIGHT = 20`, a single tick
moves at most 14px — less than one block height — so the march always
samples inside the block at least once. This is not a formal swept
guarantee; it is an implicit one from the speed ceiling.

### 1.3 Collision region logic — original/ball.c:1351-1458

`CheckRegions()` uses X11 `XRectInRegion()` on four pre-computed X11
`Region` objects per block (top/bottom/left/right triangles). The
adjacency filter is applied **per region** before OR-ing into a
bitmask:

```c
if (XRectInRegion(blockP->regionTop, ...) != RectangleOut) {
    if (blockPtop == NULL || blockPtop->occupied == False)
        region |= REGION_TOP;
}
```

Key: the legacy `CheckRegions` returns a **bitmask** (multiple regions
can be set simultaneously, e.g. `REGION_TOP | REGION_LEFT`). The
adjacency suppression is per-region and independent: a region is
suppressed only if its specific neighbor is occupied. The return value
from `CheckForCollision` is then matched against combined cases
(`REGION_BOTTOM | REGION_RIGHT`, etc.) in the switch statement at
ball.c:1251-1303.

---

## 2. Modern Algorithm (src/ball\_system.c + src/block\_system.c)

### 2.1 Position update — src/ball\_system.c:473-474

```c
b->ballx = b->oldx + b->dx;
b->bally = b->oldy + b->dy;
```

Identical to legacy.

### 2.2 Ray-march loop — src/ball\_system.c:618-701

```c
int col = ball_math_x_to_col(b->ballx, env->col_width);
int row = ball_math_y_to_row(b->bally, env->row_height);

float x_f = (float)b->oldx;
float y_f = (float)b->oldy;
/* ... compute incx, incy, step ... */
for (int j = 0; j < step; j++) {
    int ret = check_for_collision(ctx, (int)x_f, (int)y_f, &row, &col, i);
    if (ret != BALL_REGION_NONE) { /* bounce, break */ }
    x_f += incx;
    y_f += incy;
}
```

Structure is identical to legacy. Same step count, same starting point
(`oldx`, `oldy`), same sub-step loop. The modern code preserves the
legacy anti-tunneling property for the same speed ceiling reasons.

**Structural divergence from legacy:** The modern code initializes
`col`/`row` from the **new** position (`b->ballx`, `b->bally`) — same
as legacy. That cell is passed into `check_for_collision` and updated
on each hit. This matches original/ball.c:1210-1211.

### 2.3 check\_for\_collision — src/ball\_system.c:939-988

Modern version checks 9 cells (center + 8 neighbors) vs. legacy's
5-cell chain (center, +row, -row, +col, -col, +diag, -diag, +diag,
-diag). The iteration order differs but the coverage is equivalent —
both check all 8 immediate neighbors.

**Legacy bug preserved:** The last neighbor check (row-1, col+1) in
the original returns `REGION_NONE` even on a hit (original/ball.c:
1497-1501). The modern code preserves this (src/ball\_system.c:976-979).

### 2.4 block\_system\_check\_region — src/block\_system.c:527-650

This is the key function. It differs from legacy in two important ways.

#### Difference 1: Single region vs bitmask

Legacy `CheckRegions` returns a bitmask (multiple regions can be
simultaneously set). Modern `block_system_check_region` returns a
**single region value** — the first match from the diagonal
cross-product test. There is no case where two regions combine.

This means the modern switch in ball\_system.c:671-691 only handles
single-region cases (`BALL_REGION_LEFT`, `BALL_REGION_RIGHT`,
`BALL_REGION_TOP`, `BALL_REGION_BOTTOM`) and has no combined cases.
The legacy combined cases (`REGION_BOTTOM | REGION_RIGHT` etc.) at
original/ball.c:1273-1298 are dead code in the modern implementation.
This is a behavioral difference but is not the tunneling cause.

#### Difference 2: Adjacency filter logic

Legacy (original/ball.c:1390-1451): The filter is applied
**per-region** when building a bitmask. Each region independently
checks its own neighbor. If the top neighbor is occupied, `REGION_TOP`
is not set — but `REGION_BOTTOM`, `REGION_LEFT`, `REGION_RIGHT` can
still be set in the same call if their neighbors are clear.

Modern (src/block\_system.c:618-646): The filter is applied
**after the quadrant is determined** and causes the entire function to
return `BLOCK_REGION_NONE` for that block. The key code:

```c
switch (region) {
    case BLOCK_REGION_TOP:
        if (row > 0 && ctx->blocks[row - 1][col].occupied)
            return BLOCK_REGION_NONE;   /* suppress entire hit */
        break;
    case BLOCK_REGION_BOTTOM:
        if (row < MAX_ROW - 1 && ctx->blocks[row + 1][col].occupied)
            return BLOCK_REGION_NONE;   /* suppress entire hit */
        break;
    ...
}
```

---

## 3. Hypothesis Discrimination

### H1: High-speed tunneling

**Verdict: NOT the primary cause.**

Modern and legacy share identical ray-march structure with `step =
max(|dx|, |dy|)`. At the game's speed ceiling (`MAX_X_VEL=14,
MAX_Y_VEL=14`) the ball moves at most 14 pixels per tick.
`BLOCK_HEIGHT = 20`, so the march samples at least once inside any
20px-tall block. Speed normalization (`ball_math_normalize_speed`)
runs every tick. Tunneling at normal speed is geometrically impossible.
H1 would only apply if `speed_level` produced `|dy| >= BLOCK_HEIGHT`
in a single tick — which the normalization prevents.

### H2: Wrong block grid queried (stale pointer, empty grid)

**Verdict: Unlikely, but worth ruling out.**

The modern code passes `block_system_t *` as `user_data` to the
check\_region callback. If the integration layer passes a stale or
NULL block\_system pointer, `block_system_check_region` returns
`BLOCK_REGION_NONE` at the NULL check (src/block\_system.c:533) —
ball passes through everything silently. This would explain tunneling
through all blocks, not just 2 rows. The symptom (2 rows) is more
specific. H2 cannot be fully ruled out without seeing the integration
code but the pattern does not match.

### H3: Region math edge case (off-by-one in AABB)

**Verdict: Possible but not the primary mechanism.**

The modern AABB test (src/block\_system.c:559-562):

```c
if (ball_right <= bp->x || ball_left >= bp->x + bp->width ||
    ball_bottom <= bp->y || ball_top >= bp->y + bp->height)
    return BLOCK_REGION_NONE;
```

This uses strict inequality on both edges (`<=` and `>=`). A ball
exactly touching the block edge (ball\_right == bp->x) returns NONE.
The legacy XRectInRegion used different semantics. An off-by-one here
could cause misses at exact-boundary positions, but would not
systematically cause 2-row tunneling. H3 is a contributing factor to
consider alongside H4.

### H4: Adjacency-filter over-suppression (the most consistent explanation)

**Verdict: PRIMARY CAUSE — code evidence is clear.**

This is the peer-review-added hypothesis and the code confirms it.

**The mechanism:**

Consider 2 adjacent occupied rows, row N (bottom row of blocks) and
row N+1 (row below it, also occupied). The ball approaches from below,
moving upward (dy < 0).

Step 1: Ball enters row N+1's cell. The diagonal cross-product
determines `BLOCK_REGION_TOP` (ball hit the top face of row N+1's
block). The adjacency filter checks:

```c
case BLOCK_REGION_TOP:
    if (row > 0 && ctx->blocks[row - 1][col].occupied)
        return BLOCK_REGION_NONE;
```

Row N-1 (the row above N+1, which is row N) is occupied.
**Result: HIT SUPPRESSED.** The ball passes through row N+1.

Step 2: Ball continues upward into row N's cell. The diagonal
cross-product determines `BLOCK_REGION_BOTTOM` (ball hit the bottom
face of row N). The adjacency filter checks:

```c
case BLOCK_REGION_BOTTOM:
    if (row < MAX_ROW - 1 && ctx->blocks[row + 1][col].occupied)
        return BLOCK_REGION_NONE;
```

Row N+1 (below row N) is occupied.
**Result: HIT SUPPRESSED.** The ball passes through row N.

Both rows are suppressed. The ball tunnels through both.

**Does the legacy code have the same behavior?**

Legacy (original/ball.c:1422-1436, moving more vertically):

```c
if (XRectInRegion(blockP->regionBottom, ...) != RectangleOut) {
    if (blockPbottom == NULL || blockPbottom->occupied == False)
        region |= REGION_BOTTOM;
}
if (XRectInRegion(blockP->regionTop, ...) != RectangleOut) {
    if (blockPtop == NULL || blockPtop->occupied == False)
        region |= REGION_TOP;
}
```

The legacy code also suppresses REGION\_BOTTOM when the row below is
occupied. So the legacy code has the **same adjacency-filter logic**
for the same geometry. This means the tunneling is a bug that exists
in the original too — but was probably not noticed because the X11
Region geometry is different enough from the modern AABB+diagonal
approach that the ball's position relative to the region boundaries
differed, causing slightly different suppression behavior in practice.

**The critical difference between legacy and modern behavior:**

Legacy `CheckRegions` returns a bitmask. If the ball overlaps BOTH the
bottom triangle of row N AND the top triangle of row N+1 simultaneously
(possible when the ball spans the row boundary), legacy can return
`REGION_BOTTOM | REGION_TOP` — but the adjacency filter zeroes out
each individually. However, in the case where the ball overlaps both
rows and BOTH neighbors suppress their respective face, legacy also
returns `REGION_NONE` for both. The same tunneling path exists in
legacy.

The reason the bug is more visible in the modern code: the modern
`block_system_check_region` uses AABB + diagonal cross-product geometry.
With `col_width=55, row_height=32, BLOCK_HEIGHT=20`, the block is
**centered** within its grid cell (offset\_y = (32-20)/2 = 6). There
is a 6-pixel gap between the block's pixel boundary and the grid cell
boundary. The diagonal cross-product uses `by - bp->y` (relative to
the **block's** y, not the grid cell's y). When the ball is in the gap
region between two block cells, it may be below row N's block bottom
(bp->y + height) but within the AABB of row N's cell — causing the
AABB test to pass but the cross-product to classify as BOTTOM (facing
downward neighbor). That downward neighbor is occupied and the hit is
suppressed. This geometry means the ball can be in the 6px inter-block
gap and get suppressed from both sides.

---

## 4. Verdict

**H4 is the primary cause.** The adjacency filter in
`block_system_check_region` (src/block\_system.c:618-646) suppresses
hits when adjacent cells are occupied. When two rows are packed
adjacently, a ball approaching the shared face gets suppressed by both
blocks simultaneously. The ball passes through without bouncing.

**H3 contributes.** The 6px offset between grid cell boundaries and
block pixel boundaries (from centering the 20px block in a 32px cell)
creates a gap zone where the ball can be spatially between the two
block objects while still inside both their AABBs. The cross-product
then resolves to the face toward the occupied neighbor, which is then
suppressed. The gap amplifies H4.

**H1 and H2 do not match the evidence.**

---

## 5. Deterministic Reproducer

These concrete values will reproduce the tunnel in a CMocka unit test
using `ball_system_update` + `block_system_check_region`.

### 5.1 Environment parameters

```c
ball_system_env_t env = {
    .frame        = 100,
    .speed_level  = 5,          /* mid-range; normalization won't push dy below -3 */
    .paddle_pos   = 247,        /* center, irrelevant */
    .paddle_dx    = 0,
    .paddle_size  = 50,         /* MEDIUM_PADDLE */
    .play_width   = 495,        /* PLAY_WIDTH */
    .play_height  = 580,        /* PLAY_HEIGHT */
    .no_walls     = 0,
    .killer       = 0,
    .sticky_bat   = 0,
    .col_width    = 55,         /* 495 / 9 */
    .row_height   = 32,         /* 580 / 18 */
};
```

### 5.2 Grid layout (only 2 rows needed)

Place RED\_BLK at row 5 and row 6, column 4:

```text
col:   0  1  2  3  4  5  6  7  8
row 5: .  .  .  .  R  .  .  .  .   (row N)
row 6: .  .  .  .  R  .  .  .  .   (row N+1)
```

In CMocka setup:

```c
block_system_add(bsys, 5, 4, RED_BLK, 0, 100);
block_system_add(bsys, 6, 4, RED_BLK, 0, 100);
```

Block pixel geometry after `calculate_geometry`:

- Row 5, col 4: x = 4\*55 + (55-40)/2 = 220+7 = 227, y = 5\*32 + (32-20)/2 = 160+6 = 166
- Row 6, col 4: x = 227, y = 6\*32 + 6 = 198

Row 5 block occupies y=\[166, 186\]. Row 6 block occupies y=\[198, 218\].
Gap between them: y=187..197 (11px).

### 5.3 Ball initial conditions

Position the ball in the gap between the two blocks, moving upward:

```c
/* Ball at y=195: inside row 6 AABB, above row 6's block (198). */
/* BALL_WC=10, BALL_HC=9 */
int ball_x = 247;
int ball_y = 195;
int ball_dx = 0;
int ball_dy = -5;
```

To make the tunnel more reliable, position the ball just below row 6's
block bottom so it will traverse both blocks in a few ticks:

```c
int ball_x = 247;
int ball_y = 225;
int ball_dx = 2;
int ball_dy = -7;
```

### 5.4 Refined reproducer targeting the shared face

The suppression fires when:

- Ball approaches row 5 from below: hits BLOCK\_REGION\_BOTTOM on row 5, row 6 (below row 5) is occupied, SUPPRESSED.
- Ball approaches row 6 from above: hits BLOCK\_REGION\_TOP on row 6, row 5 (above row 6) is occupied, SUPPRESSED.

Position the ball between the two blocks (y in gap 187..197):

```c
/* Ball center at x=247, y=192 */
/* Row 5 block: y=[166,186]. Row 6 block: y=[198,218]. Gap: y=187..197. */
/* Ball top = 192-9=183, ball bottom = 192+10=202 */
/* Row 5 AABB: ball_bottom(202) > 166, ball_top(183) < 186 -> overlaps */
/* Row 6 AABB: ball_bottom(202) > 198, ball_top(183) < 218 -> overlaps */

int ball_x = 247;
int ball_y = 192;
int ball_dx = 1;
int ball_dy = -5;
```

For row 5 check (block y=166, h=20):

- d1 = 40\*(192-166) - 20\*(247-227) = 1040-400 = 640 > 0
- d2 = 20\*(227+40-247) - 40\*(192-166) = 400-1040 = -640 < 0
- d1>=0, d2<=0 -> BLOCK\_REGION\_BOTTOM
- Adjacency: blocks\[6\]\[4\].occupied == 1 -> return BLOCK\_REGION\_NONE

For row 6 check (block y=198, h=20):

- d1 = 40\*(192-198) - 20\*(247-227) = -240-400 = -640 < 0
- d2 = 20\*(227+40-247) - 40\*(192-198) = 400+240 = 640 > 0
- d1<=0, d2>=0 -> BLOCK\_REGION\_TOP
- Adjacency: blocks\[4\]\[4\].occupied == 0 -> HIT RETURNED

Row 4 is NOT occupied in this 2-block layout. So row 6's top hit is NOT
suppressed. The suppression on row 5's bottom IS triggered. The ball
only passes through row 5; it bounces off row 6. To get full 2-row
tunneling, use the 9-cell neighborhood scan path described next.

The key is that `check_for_collision` does a 9-cell neighborhood scan.
When the ball is in the gap at y~192, `ball_math_y_to_row(192, 32) = 6`
(the cell row). But the ball's AABB overlaps row 5's block. The loop
tries cell (row=6, col=4) first — row 6 block at y=198, ball at y=192:
ball\_top=183, ball\_bottom=202. AABB overlaps. Cross-product -> REGION\_TOP
of row 6. Row 5 (above row 6) IS occupied -> SUPPRESSED.

Then the loop tries cell (row+1=7, col=4) — not occupied -> NONE.
Then (row-1=5, col=4) — row 5, occupied. Ball at y=192, block y=166:
ball\_top=183 < 186, ball\_bottom=202 > 166. AABB overlaps.
Cross-product for row 5: d1=640>0, d2=-640<0 -> BLOCK\_REGION\_BOTTOM.
Row 6 (row 5+1) is occupied -> SUPPRESSED.

All 9 cells checked — no hit returned. Ball tunnels through.

### 5.5 Final CMocka reproducer values

```c
/* Grid: 2 adjacent rows occupied at column 4 */
block_system_add(bsys, 5, 4, RED_BLK, 0, 0);   /* row N   */
block_system_add(bsys, 6, 4, RED_BLK, 0, 0);   /* row N+1 */

ball_system_env_t env = {
    .frame = 100, .speed_level = 5,
    .paddle_pos = 247, .paddle_dx = 0, .paddle_size = 50,
    .play_width = 495, .play_height = 580,
    .no_walls = 0, .killer = 0, .sticky_bat = 0,
    .col_width = 55, .row_height = 32,
};

/* Ball in the gap zone between row 5 and row 6 blocks        */
/* Row 5 block: pixel y=[166,186].  Row 6 block: y=[198,218]. */
/* Gap: y=187..197. Ball center at y=192 is in the gap.       */
/* Ball AABB (WC=10, HC=9) at y=192: top=183, bottom=201      */
/* Overlaps row 5 AABB [160,192] and row 6 AABB [192,224]     */
int ball_x = 247;
int ball_y = 192;
int ball_dx = 1;
int ball_dy = -5;   /* moving upward through the gap */

/* After ball_system_update: assert ball_x/y has changed AND   */
/* both blocks still have hit_points == 1 (neither was broken) */
/* AND dy is still negative (no bounce happened)               */
```

**Expected buggy behavior:** ball passes through both blocks, dx/dy
unchanged, hit\_points on both blocks = 1.

**Expected fixed behavior:** ball bounces off row 6's top face (or
row 5's bottom face), at least one block takes a hit.

---

## 6. Minimal Fix Scope

**File:** `src/block_system.c`

**Function:** `block_system_check_region` (lines 618-646)

**Nature of change:** The adjacency filter must NOT suppress a hit when
the ball is between two occupied rows and hitting the face that separates
them. The fix is to suppress only when the ball is **entering from outside
the block pair**, not when it is in the gap between two adjacent occupied
blocks.

The correct interpretation of the adjacency filter: suppress a hit on face
F if the neighbor in direction F is occupied AND the ball's center is on
the far side of the boundary (i.e., the ball is between the two blocks,
not approaching from outside). Alternatively, the filter should check
whether the ball's center is actually inside the neighbor's AABB — if it
is, the hit on the current block's face is a phantom and should be
suppressed; if it is not, the hit is real and must not be suppressed.

**Concrete fix proposal:**

Replace the simple occupancy check:

```c
if (row > 0 && ctx->blocks[row - 1][col].occupied)
    return BLOCK_REGION_NONE;
```

With an occupancy + ball-position check:

```c
if (row > 0 && ctx->blocks[row - 1][col].occupied) {
    /* Only suppress if ball center is actually past this block's top edge,
     * i.e., ball is in the neighbor's territory, not in the gap */
    if (by < bp->y)     /* ball center is above block top */
        return BLOCK_REGION_NONE;
    /* otherwise fall through -- ball is in the gap, hit is real */
}
```

Apply the same pattern for BOTTOM, LEFT, RIGHT. This is ~4 lines of
change. The implement worker should write the CMocka test first to
characterize the current (broken) behavior, then apply the fix and
verify the test passes with the expected bounce.

---

## 7. Original-Source Alignment

The legacy code has the **same adjacency filter logic** at
original/ball.c:1390-1451. The bug exists in the original but was
masked by X11 Region geometry. The Xlib `XRectInRegion` with
pre-computed triangular regions produces different overlap results than
the modern AABB + diagonal cross-product. In particular, the X11 Region
objects are the exact triangle polygons — so the ball's center position
relative to the block's actual geometric face (not just its AABB) was
used. The modern AABB step passes when the ball's bounding box overlaps
the block's bounding box, which is a strictly larger set — it fires in
the gap zone where the original X11 code did not.

The fix must not change the filter's intent (prevent phantom bounces at
junctions) but must restrict it to cases where the ball is genuinely
inside the neighbor block, not merely in the gap between them.

---

## References

- `original/ball.c:1023-1315` — `UpdateABall`: position update, ray-march loop
- `original/ball.c:1351-1458` — `CheckRegions` + `CheckForCollision`
- `original/ball.c:1382-1452` — adjacency filter (per-region, bitmask)
- `src/ball_system.c:618-701` — modern ray-march loop
- `src/ball_system.c:939-988` — modern `check_for_collision` (9-cell neighborhood)
- `src/block_system.c:527-650` — `block_system_check_region` (AABB + cross-product + adjacency)
- `src/block_system.c:618-646` — adjacency filter (single-region, over-suppresses)
- `src/ball_math.c:195-203` — `ball_math_x_to_col`, `ball_math_y_to_row`
- `include/ball_types.h:15-32` — BALL\_WIDTH=20, BALL\_HEIGHT=19, BALL\_WC=10, BALL\_HC=9, MAX\_X\_VEL=14
- `include/block_system.h:52-54` — BLOCK\_WIDTH=40, BLOCK\_HEIGHT=20
