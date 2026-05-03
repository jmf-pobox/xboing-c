/*
 * regions.h — sprite-only crop regions for visual-fidelity SSIM
 * comparison (L3 of the screenshot-testing methodology).
 *
 * Source of truth for both:
 *   - L2 hash test fixtures (C consumers via this header)
 *   - L3 SSIM Python script (parses #define lines via regex)
 *
 * Coordinates are absolute mainWindow pixel positions, derived from
 * original/include/stage.h and original/stage.c CreateAllWindows()
 * with offsetX=35, scoreWidth=224, MAIN_WIDTH=70, PLAY_WIDTH=495,
 * PLAY_HEIGHT=580.
 *
 * Window total size matches both binaries: 575 wide x 720 tall
 * (PLAY_WIDTH + MAIN_WIDTH + 10  x  PLAY_HEIGHT + MAIN_HEIGHT + 10).
 *
 * Per sjl review (Q5), text-rendering regions (score digits, level
 * number digits, message bar) are NOT SSIM-comparable — X11 bitmap
 * fonts vs. SDL2_ttf TrueType lands at SSIM 0.50-0.75.  Those regions
 * are deliberately omitted.  Layer 1 unit tests
 * (level_number_digit_position, etc.) lock the digit positions.
 */

#ifndef XBOING_GOLDEN_REGIONS_H
#define XBOING_GOLDEN_REGIONS_H

/* =========================================================================
 * Window / layout constants — match original/include/stage.h
 * ========================================================================= */

#define REGION_WINDOW_W 575
#define REGION_WINDOW_H 720

#define REGION_OFFSET_X 35   /* MAIN_WIDTH / 2 — left margin */
#define REGION_PLAY_W 495    /* PLAY_WIDTH */
#define REGION_PLAY_H 580    /* PLAY_HEIGHT */
#define REGION_PLAY_X 35     /* offsetX */
#define REGION_PLAY_Y 60     /* fixed in stage.c */
#define REGION_PLAY_BORDER 2 /* playWindow XCreateSimpleWindow border_width */

/* =========================================================================
 * Region definitions — REGION_<NAME>_{X,Y,W,H} + REGION_<NAME>_SSIM
 *
 * SSIM thresholds are floors from the methodology proposal (revised).
 * The L3 Python script reads each REGION_<NAME>_SSIM value via regex
 * to look up the per-region pass threshold.
 * ========================================================================= */

/*
 * Full frame — used for attract-mode captures (presents/intro/highscore
 * screens) where the whole window is one composite.  Higher threshold
 * because backgrounds are stable static images.
 */
#define REGION_FULL_FRAME_X 0
#define REGION_FULL_FRAME_Y 0
#define REGION_FULL_FRAME_W 575
#define REGION_FULL_FRAME_H 720
#define REGION_FULL_FRAME_SSIM 0.97

/*
 * Playfield border — the 2 px red/green frame around playWindow.
 * Solid color, structural — should be near-perfect.
 */
#define REGION_PLAY_BORDER_X 33
#define REGION_PLAY_BORDER_Y 58
#define REGION_PLAY_BORDER_W 499
#define REGION_PLAY_BORDER_H 584
#define REGION_PLAY_BORDER_SSIM 0.98

/*
 * Play area interior — blocks, paddle, ball, eyedude.  The bulk of
 * gameplay rendering.  Excludes the +2 border.
 */
#define REGION_PLAY_AREA_X 35
#define REGION_PLAY_AREA_Y 60
#define REGION_PLAY_AREA_W 495
#define REGION_PLAY_AREA_H 580
#define REGION_PLAY_AREA_SSIM 0.96

/*
 * Lives panel — right-anchored lives row in the level info panel.
 * Includes ammo belt below the lives (also sprite-row).
 * Excludes the level number digits (text region per sjl Q5).
 *
 * levelWindow origin: (35+224+25, 5) = (284, 5).  Lives at y+21
 * center, ammo belt at y+43.  Width is the full levelWindow width
 * minus the digits area.
 */
#define REGION_LIVES_X 284
#define REGION_LIVES_Y 14 /* 5 + 9 (life sprite top after centring) */
#define REGION_LIVES_W 200
#define REGION_LIVES_H 50 /* covers lives row + ammo belt */
#define REGION_LIVES_SSIM 0.95

/*
 * Specials panel — power-up icons row at bottom of mainWindow.
 * specialWindow origin: (35 + 495/2 + 10, 655) = (292, 655).
 * Width 180, height MESS_HEIGHT + 5.
 *
 * Per sjl Q5 caveat: this region contains text labels (X2 / NoWalls
 * / Sticky / etc.) which would normally be excluded.  But the text
 * is bitmap-sized fixed-width and the background pattern is what
 * we actually verify (color cycling on active vs. inactive).
 * Threshold accordingly relaxed.
 */
#define REGION_SPECIALS_X 292
#define REGION_SPECIALS_Y 655
#define REGION_SPECIALS_W 180
#define REGION_SPECIALS_H 60
#define REGION_SPECIALS_SSIM 0.85

/*
 * Bonus screen coin row — the 37-px-stride row of BONUS_BLK sprites
 * during BONUS_STATE_BONUS animation.  Y is the row's baseline at
 * the time captured; X spans the full play area to handle counts
 * 1..8 coins centred.
 *
 * Geometry from src/game_render_ui.c game_render_bonus
 * (PLAY_AREA_Y + 200, full PLAY_AREA_W width).
 */
#define REGION_BONUS_COIN_ROW_X 35
#define REGION_BONUS_COIN_ROW_Y 260
#define REGION_BONUS_COIN_ROW_W 495
#define REGION_BONUS_COIN_ROW_H 30
#define REGION_BONUS_COIN_ROW_SSIM 0.95

/*
 * Bonus screen bullet row — the 10-px-stride row during
 * BONUS_STATE_BULLET.  Y is the row's baseline.
 * (PLAY_AREA_Y + 280 from game_render_ui.c.)
 */
#define REGION_BONUS_BULLET_ROW_X 35
#define REGION_BONUS_BULLET_ROW_Y 340
#define REGION_BONUS_BULLET_ROW_W 495
#define REGION_BONUS_BULLET_ROW_H 20
#define REGION_BONUS_BULLET_ROW_SSIM 0.95

#endif /* XBOING_GOLDEN_REGIONS_H */
