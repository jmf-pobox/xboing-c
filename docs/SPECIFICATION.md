# XBoing II Game Specification

> Comprehensive technical specification for XBoing, a classic X11 breakout/blockout game
> originally written by Justin C. Kibell (1993-1996), modernized for current Linux systems.

---

## Table of Contents

1. [Build System & Dependencies](#1-build-system--dependencies)
2. [Window System & Graphics](#2-window-system--graphics)
3. [Audio System](#3-audio-system)
4. [Game State Machine & Flow Control](#4-game-state-machine--flow-control)
5. [Ball Physics & Collision](#5-ball-physics--collision)
6. [Block System](#6-block-system)
7. [Paddle](#7-paddle)
8. [Gun/Bullet System](#8-gunbullet-system)
9. [Scoring & Bonuses](#9-scoring--bonuses)
10. [Special Effects & Power-ups](#10-special-effects--power-ups)
11. [Level System](#11-level-system)
12. [Level Editor](#12-level-editor)
13. [High Score System](#13-high-score-system)
14. [Save/Load System](#14-saveload-system)
15. [UI Screens & Animation Sequences](#15-ui-screens--animation-sequences)
16. [Module Dependency Map](#16-module-dependency-map)

---

## 1. Build System & Dependencies

### Compiler & Flags

| Setting | Value |
|---------|-------|
| Compiler | `gcc` |
| CFLAGS | `-O2 -Wall -Wno-unused-result -Wno-format-overflow -Wno-format-truncation` |
| Include paths | `-I./include -I/usr/include/X11` |
| LDFLAGS | (empty) |
| Libraries | `-lXpm -lX11 -lm` |

**System dependencies:** `libxpm-dev`, `libx11-dev`, GCC.

### Build-Time Defines

| Define | Default Value | Purpose |
|--------|---------------|---------|
| `HIGH_SCORE_FILE` | `./.xboing.scr` | High score file path |
| `LEVEL_INSTALL_DIR` | `./levels` | Level data files directory |
| `SOUNDS_DIR` | `./sounds` | Audio files directory |
| `READMEP_FILE` | `./docs/problems.doc` | Documentation file |
| `AUDIO_AVAILABLE` | `True` | Enable audio support |
| `AUDIO_FILE` | `audio/LINUXaudio.c` | Audio driver source file |
| `NeedFunctionPrototypes` | `1` | Enable ANSI C prototypes |

### Runtime Environment Variable Overrides

| Env Variable | Compile-Time Default | Purpose |
|---|---|---|
| `XBOING_SCORE_FILE` | `HIGH_SCORE_FILE` | High score storage location |
| `XBOING_LEVELS_DIR` | `LEVEL_INSTALL_DIR` | Level files directory |
| `XBOING_SOUND_DIR` | `SOUNDS_DIR` | Sound files directory |

### Build Targets

- **`all`** (default): Generates `audio.c` symlink, `version.c`, compiles all `.c` files, links `xboing`
- **`version.c`**: Generated dynamically by `version.sh` script
- **`audio.c`**: Symlink created from `audio/LINUXaudio.c`
- **`clean`**: Removes object files, binary, and generated files

### Source Files (29 total)

`version.c`, `main.c`, `score.c`, `error.c`, `ball.c`, `blocks.c`, `init.c`, `stage.c`, `level.c`, `paddle.c`, `mess.c`, `intro.c`, `bonus.c`, `sfx.c`, `highscore.c`, `misc.c`, `inst.c`, `gun.c`, `keys.c`, `audio.c`, `special.c`, `presents.c`, `demo.c`, `file.c`, `preview.c`, `dialogue.c`, `eyedude.c`, `editor.c`, `keysedit.c`

### Version Generation (`version.sh`)

Generates `version.c` with:
- `char *dateString` — build timestamp (from `date`)
- `char *whoString` — build user (from `$USER` or "root")
- `char *machineString` — architecture (from `uname -a`)
- `int buildNum` — auto-incrementing build counter (stored in `.version` file)

### Command-Line Options

| Option | Argument | Range | Effect |
|--------|----------|-------|--------|
| `-display` | `<name>` | any string | Set X display connection |
| `-help` | — | — | Print detailed help, exit |
| `-usage` | — | — | Print brief usage, exit |
| `-version` | — | — | Print version, exit |
| `-setup` | — | — | Print setup/path info, exit |
| `-sync` | — | — | Enable X protocol synchronization |
| `-debug` | — | — | Debug mode (sets nickname to "Debug Mode") |
| `-sound` | — | — | Enable audio (default is OFF) |
| `-nosfx` | — | — | Disable special visual effects |
| `-keys` | — | — | Paddle control via keyboard (default: mouse) |
| `-grab` | — | — | Grab pointer to window |
| `-noicon` | — | — | Disable custom window icon |
| `-usedefcmap` | — | — | Use default colormap instead of private |
| `-scores` | — | — | Print high scores to stdout, exit |
| `-speed` | `<1-9>` | 1-9 | Game speed (9 = fastest) |
| `-startlevel` | `<1-80>` | 1-MAX_NUM_LEVELS | Starting level |
| `-maxvol` | `<1-100>` | 1-100 | Maximum audio volume percentage |
| `-nickname` | `<name>` | max 20 chars | Player nickname |

**Default settings:** sync=off, debug=off, grabPointer=off, useDefaultColourmap=off, noSound=on (audio off by default), noicon=off, nickname="" (uses real name), maxVolume=0 (system default), startLevel=1, userSpeed=5, useSpecialEffects=on, score=0.

---

## 2. Window System & Graphics

### X11 Initialization Sequence

1. Open X display connection
2. Set close-down mode: `DestroyAll`
3. Enable X synchronization if `-sync` flag
4. Set custom error handler (`ErrorHandler`)
5. Install signal handlers: SIGINT (abort), SIGSEGV (core dump in debug mode)
6. Seed random number generator with `time(NULL)`
7. Select color visual (see below)
8. Create/select colormap
9. Initialize audio (if enabled)
10. Map named colors
11. Create all windows and sub-windows
12. Initialize 6 graphics contexts
13. Load 4 fonts (with "fixed" fallback)
14. Set window backgrounds from XPM pixmaps
15. Initialize all subsystem pixmaps (messages, blocks, balls, bullets, score digits, levels, paddle, dialogue, eyedudes, presents, keys, editor, instructions, intro, bonus, highscore)
16. Create color cycling arrays
17. Select input events on main and play windows
18. Map main window
19. Install colormap

### Visual Selection

Attempted in order (first match wins):
1. **PseudoColor** — indexed color
2. **DirectColor** — direct RGB with hardware colormap
3. **TrueColor** — direct RGB without colormap

Fails if no color visual is available.

### Window Hierarchy

**Main window:** `565 x 710` pixels (PLAY_WIDTH + MAIN_WIDTH + 10 x PLAY_HEIGHT + MAIN_HEIGHT + 10)

| Window | Size (px) | Position | Purpose |
|--------|-----------|----------|---------|
| `mainWindow` | 565 x 710 | (0, 0) | Top-level application window |
| `scoreWindow` | 224 x 42 | (247, 10) | Current score display |
| `levelWindow` | 224 x 52 | (496, 5) | Level number, lives, ammo |
| `playWindow` | 495 x 580 | (247, 60) | Main gameplay area (red border, width 2) |
| `bufferWindow` | 495 x 580 | (247, 60) | Offscreen rendering buffer (hidden) |
| `messWindow` | 247 x 30 | (247, 655) | Game status messages |
| `specialWindow` | 180 x 35 | (496, 655) | Active specials status display |
| `timeWindow` | 61 x 35 | (682, 655) | Time bonus countdown (MM:SS) |
| `blockWindow` | 120 x 580 | (757, 60) | Editor tool palette (editor only) |
| `typeWindow` | 120 x 35 | (757, 660) | Editor block type indicator (editor only) |
| `inputWindow` | 380 x 120 | centered | Modal input dialog |

**Window constants:**

| Constant | Value |
|----------|-------|
| `MAIN_WIDTH` | 70 |
| `MAIN_HEIGHT` | 130 |
| `PLAY_WIDTH` | 495 |
| `PLAY_HEIGHT` | 580 |
| `MESS_HEIGHT` | 30 |
| `TYPE_HEIGHT` | 35 |

**Window manager properties:**
- Window name: "- XBoing II -"
- Icon name: "XBoing II"
- WM class: "XBoing"
- Size locked (min = max, no resizing)
- Backing store set to `Always` if available (required for SFX)

### Graphics Contexts

| GC | X Function | Purpose |
|----|------------|---------|
| `gccopy` | `GXcopy` | Standard copy operation |
| `gc` | `GXcopy` | Tiled fill operations (FillTiled) |
| `gcxor` | `GXxor` | XOR mode (reversible drawing) |
| `gcand` | `GXand` | AND mode (fg=0, bg=~0) |
| `gcor` | `GXor` | OR mode (combining images) |
| `gcsfx` | `GXcopy` | Special effects drawing |

All GCs have `graphics_exposures = False`.

### Fonts

| Variable | XLFD Pattern | Size | Purpose |
|----------|-------------|------|---------|
| `titleFont` | `-adobe-helvetica-bold-r-*-*-24-*-*-*-*-*-*-*` | 24pt bold | Title text |
| `textFont` | `-adobe-helvetica-medium-r-*-*-18-*-*-*-*-*-*-*` | 18pt medium | General text |
| `dataFont` | `-adobe-helvetica-bold-r-*-*-14-*-*-*-*-*-*-*` | 14pt bold | Data/intro text |
| `copyFont` | `-adobe-helvetica-medium-r-*-*-12-*-*-*-*-*-*-*` | 12pt medium | Copyright text |

Fallback: `"fixed"` font if any load fails.

### Named Colors

| Variable | Color Name |
|----------|-----------|
| `red` | "red" |
| `tann` | "tan" |
| `yellow` | "yellow" |
| `green` | "green" |
| `purple` | "purple" |
| `blue` | "blue" |
| `white` | System WhitePixel |
| `black` | System BlackPixel |

**Color cycling arrays:**
- `reds[7]` — red shades from `#f00` down to `#300` (7 levels, index 0-6)
- `greens[7]` — green shades from `#0f0` down to `#030` (7 levels, index 0-6)

### Cursor Types

| Constant | Value | X Cursor | Purpose |
|----------|-------|----------|---------|
| `CURSOR_WAIT` | 1 | `XC_watch` | Wait/loading |
| `CURSOR_PLUS` | 2 | `XC_plus` | Crosshair/selection |
| `CURSOR_NONE` | 3 | Custom 1x1 pixmap | Hidden/invisible |
| `CURSOR_POINT` | 4 | `XC_hand2` | Pointing hand |
| `CURSOR_SKULL` | 5 | `XC_pirate` | Skull/danger |

### Background Types

| Constant | Value | Description |
|----------|-------|-------------|
| `BACKGROUND_WHITE` | -2 | Solid white fill |
| `BACKGROUND_BLACK` | -1 | Solid black fill |
| `BACKGROUND_0` | 0 | Main background pixmap |
| `BACKGROUND_1` | 1 | Alternate background 1 |
| `BACKGROUND_2` | 2 | Alternate background 2 |
| `BACKGROUND_3` | 3 | Alternate background 3 |
| `BACKGROUND_4` | 4 | Alternate background 4 |
| `BACKGROUND_5` | 5 | Alternate background 5 |
| `BACKGROUND_SEE_THRU` | 10 | ParentRelative (transparent) |
| `BACKGROUND_SPACE` | 11 | Space/stars pattern |

### Bitmap Asset Directory Structure

```
bitmaps/
  balls/         Ball animation frames (ball1-4.xpm, killer.xpm, bbirth1-8.xpm)
  bgrnds/        Background pixmaps (6 backgrounds + space)
  blockex/       Block explosion frames
  blocks/        Block type pixmaps (30+ types)
  digits/        Score digit pixmaps (0-9, 30x40 each)
  eyes/          Devil eyes animation (6 frames, 57x16 each)
  guides/        Ball launch guide indicators (11 frames)
  guns/          Bullet and tink pixmaps
  paddle/        Paddle pixmaps (3 sizes)
  presents/      Presents sequence pixmaps (flag, letters, title)
  stars/         Star/sparkle animation frames (11 frames)
  floppy.xpm     Save icon
  highscr.xpm    High score title
  icon.xpm       Window icon
  larrow.xpm     Left arrow (35x19)
  rarrow.xpm     Right arrow (35x19)
  mouse.xpm      Mouse pixmap (35x57)
  question.xpm   Question mark icon
  text.xpm       Text icon
```

---

## 3. Audio System

### Pluggable Driver Architecture

12 audio drivers available in `audio/`:

| Driver File | Platform |
|-------------|----------|
| `LINUXaudio.c` | Linux (default, `/dev/dsp`) |
| `ALSAaudio.c` | ALSA (Advanced Linux Sound Architecture) |
| `BSDaudio.c` | BSD |
| `HPaudio.c` | HP-UX |
| `SGIaudio.c` | SGI IRIX |
| `SUNaudio.c` | SunOS/Solaris |
| `SVR4audio.c` | System V Release 4 |
| `AFaudio.c` | Audio Devices (AF) |
| `RPLAYaudio.c` | RPLAY (remote audio) |
| `NCDaudio.c` | NCD (Network Computing Devices) |
| `LINUXaudio2.c` | Linux alternate |
| `NOaudio.c` | Silent/null (no audio) |

The `audio.c` symlink in the project root points to the active driver (default: `audio/LINUXaudio.c`).

### Linux Driver Implementation

**Architecture:** Fork + pipe IPC. The main process forks a child that listens for sound file names on a pipe, plays them to `/dev/dsp`.

**Buffer size:** 32 KB (`SBUF_SIZE=32`, `BUFFER_SIZE = 1024 * SBUF_SIZE`)

**Sound file format:** Sun `.au` files, located in `SOUNDS_DIR` (or `XBOING_SOUND_DIR` env override). Path construction: `{SOUNDS_DIR}/{filename}.au`.

**Device control:** Uses Linux `<linux/soundcard.h>` ioctls:
- `SNDCTL_DSP_SYNC` — flush audio device after each file
- `SNDCTL_DSP_RESET` — reset device on exit

**Termination:** Parent sends "EXIT" string on pipe; child closes device and exits.

**Note:** Volume control is not implemented in the Linux driver (`SetMaximumVolume` and `GetMaximumVolume` are no-ops).

### Audio API

| Function | Signature | Purpose |
|----------|-----------|---------|
| `SetUpAudioSystem` | `int SetUpAudioSystem(Display *display)` | Initialize audio (fork child) |
| `FreeAudioSystem` | `void FreeAudioSystem(void)` | Shutdown audio (send EXIT) |
| `playSoundFile` | `void playSoundFile(char *filename, int volume)` | Play a sound file |
| `audioDeviceEvents` | `void audioDeviceEvents(void)` | Handle audio device events (no-op on Linux) |
| `SetMaximumVolume` | `void SetMaximumVolume(int Volume)` | Set max volume (1-100) |
| `GetMaximumVolume` | `int GetMaximumVolume(void)` | Get max volume |

### Sound Files (46 `.au` files)

`ammo`, `applause`, `ball2ball`, `balllost`, `ballshot`, `boing`, `bomb`, `bonus`, `buzzer`, `click`, `ddloo`, `Doh1`, `Doh2`, `Doh3`, `Doh4`, `evillaugh`, `game_over`, `gate`, `hithere`, `hypspc`, `intro`, `key`, `looksbad`, `metal`, `mgun`, `ouch`, `paddle`, `ping`, `shark`, `shoot`, `shotgun`, `spring`, `stamp`, `sticky`, `supbons`, `toggle`, `tone`, `touch`, `wallsoff`, `warp`, `weeek`, `whizzo`, `whoosh`, `wzzz`, `wzzz2`, `youagod`

---

## 4. Game State Machine & Flow Control

### Mode Enumeration (16 modes)

| ID | Enum | Description |
|----|------|-------------|
| 0 | `MODE_NONE` | Uninitialized |
| 1 | `MODE_HIGHSCORE` | High score display |
| 2 | `MODE_INTRO` | Introduction/splash |
| 3 | `MODE_GAME` | Active gameplay |
| 4 | `MODE_PAUSE` | Game paused |
| 5 | `MODE_BALL_WAIT` | Waiting for ball activation |
| 6 | `MODE_WAIT` | Generic wait |
| 7 | `MODE_BONUS` | Bonus/level complete screen |
| 8 | `MODE_INSTRUCT` | Instructions display |
| 9 | `MODE_KEYS` | Key configuration display |
| 10 | `MODE_PRESENTS` | Presents/credits (startup) |
| 11 | `MODE_DEMO` | Demo/auto-play |
| 12 | `MODE_PREVIEW` | Level preview |
| 13 | `MODE_DIALOGUE` | Modal dialog |
| 14 | `MODE_EDIT` | Level editor |
| 15 | `MODE_KEYSEDIT` | Editor key bindings display |

### Event Loop Architecture

```
main()
  -> InitialiseGame() -> Display*
  -> SetGameSpeed(FAST_SPEED)
  -> handleEventLoop(display)
       |
       +-> Wait for MapNotify
       +-> Grab pointer
       +-> Loop forever:
            |
            +-> audioDeviceEvents()
            +-> If not iconified && mode != PAUSE:
            |     XPending() [non-blocking]
            |     frame++ (unless DIALOGUE)
            +-> Else:
            |     XPeekEvent() [blocking wait]
            |     XPending()
            +-> Dispatch events:
            |     KeyPress -> handleKeyPress()
            |     ButtonPress/Release -> handleMouseButtons()
            |     Expose -> SelectiveRedraw()
            |     MapNotify -> enable events, grab pointer
            |     UnmapNotify -> reduce events, pause
            +-> If not iconified:
            |     MODE_GAME/BALL_WAIT: sleepSync(display, speed)
            |     Other modes: XSync(display, False)
            +-> If not iconified:
                  handleGameStates() -> mode dispatch
                  XFlush()
```

### Frame Timing

| Constant | Value | Description |
|----------|-------|-------------|
| `FAST_SPEED` | 5 ms | Fastest game speed |
| `MEDIUM_SPEED` | 15 ms | Medium speed |
| `SLOW_SPEED` | 30 ms | Slowest speed |
| `PADDLE_ANIMATE_DELAY` | 5 | Frames between paddle updates |
| `BONUS_SEED` | 2000 | Random interval for bonus spawning |
| `MAX_TILTS` | 3 | Maximum board tilts per level |

`sleepSync()` implementation: `XSync(display, False)` followed by `usleep(ms * 400)`.

Speed keys 1-9 map inversely: key 1 = `SetUserSpeed(9)` (slowest), key 9 = `SetUserSpeed(1)` (fastest). Default speed: 5.

### State Transitions

**Startup flow:**
```
MODE_PRESENTS -> MODE_INTRO
```

**Menu cycle** (C key advances):
```
INTRO -> INSTRUCT -> DEMO -> KEYS -> KEYSEDIT -> HIGHSCORE -> PREVIEW -> INTRO
```

**Gameplay transitions:**
```
Any menu mode  --[Space]-->  MODE_GAME
MODE_GAME      --[P]------>  MODE_PAUSE
MODE_PAUSE     --[P]------>  MODE_GAME
MODE_GAME      --[level complete]-->  MODE_BONUS
MODE_BONUS     --[sequence end]---->  MODE_GAME (next level)
MODE_GAME      --[all lives lost]-->  MODE_HIGHSCORE
MODE_GAME      --[Escape]--------->  MODE_DIALOGUE -> MODE_INTRO
```

### Game Mode Handler (`handleGameMode`)

Per-frame processing during `MODE_GAME`:

1. **First entry** (gameActive == False): Set level, lives=3, score=0, paddle=HUGE, tilts=0, load level via `SetupStage()`
2. **Paddle** (every 5 frames): `handlePaddleMoving()`
3. **Ball physics** (every frame): `HandleBallMode()`
4. **Bonus block spawning**: Random 27-type selection at `frame + (rand() % 2000)` intervals
5. **Bullet handling**: `HandleBulletMode()`
6. **Block explosions**: `ExplodeBlocksPending()`
7. **Animations**: `HandlePendingAnimations()`
8. **Eye dude enemy**: `HandleEyeDudeMode()`
9. **Game rules check**: `CheckGameRules()` (extra lives, timer, level completion)

### Input Handling

**Paddle control modes:**
- `CONTROL_KEYS` (0): Arrow keys / J, L keys. `paddleMotion` = -1 (left), 0 (stop), 1 (right).
- `CONTROL_MOUSE` (1): Pointer follow via `ObtainMousePosition()`. Calculates `paddleDx` delta.

**Mouse buttons:** Button1/2/3 all treated identically in gameplay — activate waiting ball or shoot bullet.

### Complete Key Binding Table

#### Game Mode Keys (MODE_GAME, MODE_PAUSE, MODE_BALL_WAIT, MODE_WAIT)

| Key(s) | Action |
|--------|--------|
| Left Arrow, J, j | Paddle left |
| Right Arrow, L, l | Paddle right |
| K, k | Shoot bullet or activate waiting ball |
| T, t | Board tilt (max 3 per level) |
| D, d | Clear active ball |
| P, p | Toggle pause |
| Z, z | Save game (if save enabled) |
| X, x | Load saved game |
| = | Skip level (debug mode only) |
| Escape | Abort game (opens quit dialog) |

#### Menu Mode Keys (MODE_INTRO, MODE_INSTRUCT, MODE_DEMO, etc.)

| Key(s) | Action |
|--------|--------|
| Space | Start game |
| C, c | Cycle through intro screens |
| H | Personal high scores |
| h | Global high scores |
| E, e | Level editor |
| W, w | Change starting level |
| S, s | Toggle special effects |

#### Universal Keys (all modes except DIALOGUE)

| Key(s) | Action |
|--------|--------|
| 1-9 | Set game speed (1=slow, 9=fast) |
| +, KP_Add | Volume up |
| -, KP_Subtract | Volume down |
| A, a | Toggle audio on/off |
| G, g | Toggle keyboard/mouse control |
| I, i | Iconify window |
| Q, q | Quit to system |

### Pause Mechanics

- Entry: records `pauseStartTime`, displays "- Game paused -", ungrabs pointer
- Exit: accumulates `pausedTime += (now - pauseStartTime)`, displays "- Play ball -", re-grabs pointer
- While paused: frame counter frozen, event loop blocks on `XPeekEvent()`

### Iconification

- `UnmapNotify` triggers pause and reduced event mask
- `MapNotify` restores full event mask and grabs pointer
- Frame counter frozen while iconified

---

## 5. Ball Physics & Collision

### Ball Structure

```c
typedef struct {
    enum BallStates waitMode;       // Waiting sub-state
    int   waitingFrame;             // Frame to wait until
    int   newMode;                  // Destination mode
    int   nextFrame;                // Next frame trigger
    int   active;                   // True if ball in use
    int   oldx, oldy;              // Previous position
    int   ballx, bally;            // Current position (center)
    int   dx, dy;                  // Velocity components
    int   slide;                   // Animation frame index (0-4)
    float radius;                  // Geometric radius
    float mass;                    // Mass (1.0-3.0)
    int   lastPaddleHitFrame;      // Frame of last paddle contact
    enum BallStates ballState;     // Current state
} BALL;
```

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `MAX_BALLS` | 5 | Maximum simultaneous balls |
| `BALL_WIDTH` | 20 | Ball sprite width (px) |
| `BALL_HEIGHT` | 19 | Ball sprite height (px) |
| `BALL_WC` | 10 | Width center offset |
| `BALL_HC` | 9.5 | Height center offset |
| `MAX_X_VEL` | 14 | Maximum X velocity (px/frame) |
| `MAX_Y_VEL` | 14 | Maximum Y velocity (px/frame) |
| `MIN_DX_BALL` | 2 | Minimum X velocity |
| `MIN_DY_BALL` | 2 | Minimum Y velocity |
| `MAX_BALL_MASS` | 3.0 | Maximum ball mass |
| `MIN_BALL_MASS` | 1.0 | Minimum ball mass |
| `BALL_AUTO_ACTIVE_DELAY` | 3000 | Auto-launch delay (ms) |
| `PADDLE_HIT_SCORE` | 10 | Points per paddle hit |
| `DIST_BALL_OF_PADDLE` | 45 | Ball offset above paddle (px) |
| `PADDLE_BALL_FRAME_TILT` | 5000 | Auto-tilt frame threshold |
| `BALL_FRAME_RATE` | 5 | Frames between physics updates |
| `BALL_ANIM_RATE` | 50 | Frames between animation cycles |
| `BIRTH_SLIDES` | 8 | Birth animation frame count |
| `BALL_SLIDES` | 5 | Active ball animation frames |
| `BIRTH_FRAME_RATE` | 5 | Birth animation frame interval |
| `BORDER_ANIM_DELAY` | 15 | Border collision effect delay |

### Ball State Machine

| State | Value | Description |
|-------|-------|-------------|
| `BALL_POP` | 0 | Pop/dying animation |
| `BALL_ACTIVE` | 1 | Moving in play arena |
| `BALL_STOP` | 2 | Stopped (dead) |
| `BALL_CREATE` | 3 | Birth animation |
| `BALL_DIE` | 4 | Going to die |
| `BALL_WAIT` | 5 | Transitioning between modes |
| `BALL_READY` | 6 | On paddle, waiting for launch |
| `BALL_NONE` | 7 | Null/undefined |

**Lifecycle:** `BALL_NONE` -> `BALL_CREATE` (8-frame birth animation) -> `BALL_READY` (on paddle, auto-launch after 3000ms) -> `BALL_ACTIVE` (in play) -> `BALL_DIE`/`BALL_POP` (8-frame reverse animation) -> `BALL_NONE`

### Paddle Collision

Geometric line intersection between ball trajectory and paddle line at `PLAY_HEIGHT - DIST_BASE - 2`:

**Case A — vertical ball (dx == 0):** Check if `ballx` is within paddle bounds.

**Case B — diagonal ball (dx != 0):**
1. Calculate trajectory line: `y = alpha * x + beta`
2. Find intersection with paddle line: `xH = (paddleLine - beta) / alpha`
3. Check if `xH` within paddle bounds (with `BALL_WC` margin)

**Bounce angle calculation:**
```
Vs = sqrt(Vx^2 + Vy^2)          // Speed magnitude
alpha = atan(Vx / -Vy)           // Current angle
beta = atan(hitPos / padSize)    // Reflection angle from hit position
gamma = 2 * beta - alpha         // New direction

Vx_new = Vs * sin(gamma)
Vy_new = -Vs * cos(gamma)
Vx_new += (paddleDx / 10.0)     // Add paddle momentum
```

Minimum upward velocity enforced: `dy >= -MIN_DY_BALL`.

### Block Collision (4-Region System)

Each block has 4 triangular X11 Regions (top, bottom, left, right). Collision detection uses `XRectInRegion()`:

1. Convert ball position to grid coordinates
2. Step through ball trajectory path
3. At each step, test ball bounding box against block regions
4. Priority based on ball direction: `|dx| > |dy|` checks horizontal first, else vertical first
5. Adjacent block checks before confirming collision direction

**Region constants:**
- `REGION_NONE` = 0
- `REGION_TOP` = 1
- `REGION_BOTTOM` = 2
- `REGION_LEFT` = 4
- `REGION_RIGHT` = 8
- Corner hits combine flags (e.g., `REGION_LEFT | REGION_TOP`)

**Bounce variation:** Random adjustment `r = (rand() >> 16) % 4` applied to velocity after bounce.

### Ball-to-Ball Collision

**Detection:** Quadratic equation for collision time prediction.

```
p = position_delta (ball1 - ball2)
v = velocity_delta (ball1 - ball2)
v2 = v.x^2 + v.y^2
r2 = (radius1 + radius2)^2
discriminant = (v2 * r2) - ((v.x * p.y) - (v.y * p.x))^2
```

If `discriminant >= 0` and `v2 > MACHINE_EPS`: collision predicted. Calculate `tmin` via quadratic formula; if `0.0 <= tmin <= 1.0`, collision occurs this frame.

**Response:** Elastic collision with mass-based velocity exchange.
```
massrate = ball1->mass / ball2->mass
k = -2.0 * (v dot p) / (1.0 + massrate)
ball1->dv += k * p_normalized
ball2->dv += k * (-massrate) * p_normalized
```

`MACHINE_EPS = sqrt(1.40129846432481707e-45)` prevents numerical instability.

### Wall/Boundary Collisions

| Boundary | Condition | noWalls=False | noWalls=True |
|----------|-----------|---------------|--------------|
| Left | `ballx < BALL_WC` | Reverse X: `dx = abs(dx)` | Wrap to right: `ballx = PLAY_WIDTH - BALL_WC` |
| Right | `ballx > PLAY_WIDTH - BALL_WC` | Reverse X: `dx = -abs(dx)` | Wrap to left: `ballx = BALL_WC` |
| Top | `bally < BALL_HC` | Reverse Y: `dy = abs(dy)` | Reverse Y: `dy = abs(dy)` |
| Bottom | `bally > PLAY_HEIGHT + BALL_HEIGHT*2` | Ball dies | Ball dies |

Wall bounces play "boing" sound.

### Animation Frames

- **Active ball:** 5 frames (`ball1.xpm` through `ball4.xpm` + `killer.xpm`), cycling every 50 frames
- **Birth/pop:** 8 frames (`bbirth1.xpm` through `bbirth8.xpm`), transitioning every 5 frames
- **Launch guide:** 11 frames (`guide1.xpm` through `guide11.xpm`), positions 0-10 mapping to launch angles from (-5,-1) leftmost to (5,-1) rightmost, with (0,-5) straight up at position 5

### Teleport (Hyperspace Block)

1. Loop up to 20 random attempts to find unoccupied block position
2. Check all 4 adjacent blocks are not BLACK_BLK
3. Place ball at block center
4. `RandomiseBallVelocity()` — random velocity [+/-3 to +/-14] on each axis
5. If 20 attempts exhausted: return ball to paddle

### Auto-Tilt

If `lastPaddleHitFrame` exceeded without paddle contact (ball stuck in loop), auto-tilt triggers `RandomiseBallVelocity()` and displays "Auto Tilt Activated".

---

## 6. Block System

### Grid Dimensions

| Constant | Value |
|----------|-------|
| `MAX_ROW` | 18 |
| `MAX_COL` | 9 |
| `BLOCK_WIDTH` | 40 |
| `BLOCK_HEIGHT` | 20 |
| `MAX_BLOCKS` | 30 (total types) |
| `MAX_STATIC_BLOCKS` | 25 (base types) |
| `SPACE` | 7 |

Grid stored as `struct aBlock blocks[MAX_ROW][MAX_COL]` — an 18x9 array.

### Complete Block Type Table

| ID | Enum | Level Char | Dimensions | Points | Behavior |
|----|------|-----------|------------|--------|----------|
| 0 | `RED_BLK` | `r` | 40x20 | 100 | Standard breakable |
| 1 | `BLUE_BLK` | `b` | 40x20 | 110 | Standard breakable |
| 2 | `GREEN_BLK` | `g` | 40x20 | 120 | Standard breakable |
| 3 | `TAN_BLK` | `t` | 40x20 | 130 | Standard breakable |
| 4 | `YELLOW_BLK` | `y` | 40x20 | 140 | Standard breakable |
| 5 | `PURPLE_BLK` | `p` | 40x20 | 150 | Standard breakable |
| 6 | `BULLET_BLK` | `B` | 40x20 | 50 | Ammo pickup |
| 7 | `BLACK_BLK` | `w` | 50x30 | 0 | Indestructible wall |
| 8 | `COUNTER_BLK` | `0`-`5` | 40x20 | 200 | Multi-hit countdown (0-5 hits) |
| 9 | `BOMB_BLK` | `X` | 30x30 | 50 | Explodes on hit (chain) |
| 10 | `DEATH_BLK` | `D` | 30x30 | 0 | Instant ball kill |
| 11 | `REVERSE_BLK` | `R` | 33x16 | 100 | Reverses paddle controls |
| 12 | `HYPERSPACE_BLK` | `H` | 31x31 | 100 | Teleports ball |
| 13 | `EXTRABALL_BLK` | `L` | 30x19 | 100 | Extra life |
| 14 | `MGUN_BLK` | `M` | 35x15 | 100 | Machine gun power-up |
| 15 | `WALLOFF_BLK` | `W` | 27x23 | 100 | Disables walls |
| 16 | `MULTIBALL_BLK` | `m` | 40x20 | 100 | Splits ball in two |
| 17 | `STICKY_BLK` | `s` | 32x27 | 100 | Sticky paddle |
| 18 | `PAD_SHRINK_BLK` | `<` | 40x15 | 100 | Shrinks paddle |
| 19 | `PAD_EXPAND_BLK` | `>` | 40x15 | 100 | Expands paddle |
| 20 | `DROP_BLK` | `d` | 40x20 | (18-row)*100 | Falls when hit (200-1800 pts) |
| 21 | `MAXAMMO_BLK` | `c` | 40x20 | 50 | Max ammo pickup |
| 22 | `ROAMER_BLK` | `+` | 25x27 | 400 | Mobile roaming block |
| 23 | `TIMER_BLK` | `T` | 21x21 | 100 | Extra time bonus (+20 sec) |
| 24 | `RANDOM_BLK` | `?` | 40x20 | 0 | Random type on load |
| 25 | `DYNAMITE_BLK` | — | 40x20 | 0 | Chain explosion (bonus only) |
| 26 | `BONUSX2_BLK` | — | 27x27 | 0 | 2x score multiplier (bonus only) |
| 27 | `BONUSX4_BLK` | — | 27x27 | 0 | 4x score multiplier (bonus only) |
| 28 | `BONUS_BLK` | — | 27x27 | 0 | Bonus coin (bonus only) |
| 29 | `BLACKHIT_BLK` | — | 50x30 | 0 | Wall hit indicator (internal) |

Special values: `NONE_BLK` = -2 (empty), `KILL_BLK` = -1 (destroyed).

### Block Data Structure

```c
struct aBlock {
    int occupied;               // Block exists
    int blockType;              // Type ID (0-29)
    int hitPoints;              // Points awarded

    int exploding;              // Currently exploding
    int explodeStartFrame;      // Explosion start frame
    int explodeNextFrame;       // Next explosion frame
    int explodeSlide;           // Explosion animation index

    int currentFrame;           // Current animation frame
    int nextFrame;              // Next frame to display
    int lastFrame;              // Last animation frame

    int blockOffsetX, blockOffsetY;
    int x, y;                   // Pixel position
    int width, height;          // Pixel dimensions

    Region regionTop, regionBottom, regionLeft, regionRight;

    int counterSlide;           // Counter state (0-5)
    int bonusSlide;             // Bonus animation state
    int random;                 // Random block flag
    int drop;                   // Dropping flag
    int specialPopup;           // Special popup flag
    int explodeAll;             // Chain explosion flag
    int ballHitIndex;           // Multiball split index
    int balldx, balldy;        // Ball velocity on split
};
```

### Collision Region Setup

Each block has 4 triangular collision regions created via `XPolygonRegion()`:

| Region | Vertices |
|--------|----------|
| `regionTop` | (x,y) -> (halfX,halfY) -> (x+width,y) |
| `regionBottom` | (x,y+height) -> (halfX,halfY) -> (x+width,y+height) |
| `regionLeft` | (x,y) -> (halfX,halfY) -> (x,y+height) |
| `regionRight` | (x+width,y) -> (halfX,halfY) -> (x+width,y+height) |

### Explosion Animation

| Constant | Value |
|----------|-------|
| `EXPLODE_DELAY` | 10 frames |
| `BONUS_DELAY` | 150 frames |
| `BONUS_LENGTH` | 1500 frames |
| `DEATH_DELAY1` | 100 frames |
| `DEATH_DELAY2` | 700 frames |
| `EXTRABALL_DELAY` | 300 frames |
| `RANDOM_DELAY` | 500 frames |
| `DROP_DELAY` | 1000 frames |
| `ROAM_EYES_DELAY` | 300 frames |
| `ROAM_DELAY` | 1000 frames |
| `INFINITE_DELAY` | 9999999 frames |
| `EXTRA_TIME` | 20 seconds |

Explosion animation: 3 frames, `EXPLODE_DELAY` = 10 frames between each.

### Block-Ball Interaction by Type

| Block Type | Ball Effect | Bounces? |
|-----------|------------|----------|
| Standard colors | Block destroyed | Yes |
| `COUNTER_BLK` | Decrements counter; kills at 0 | Yes (unless Killer mode) |
| `DEATH_BLK` | Ball killed instantly | No |
| `HYPERSPACE_BLK` | Ball teleported randomly | No |
| `MULTIBALL_BLK` | Ball splits in two | Yes |
| `BLACK_BLK` | Block survives (shows BLACKHIT indicator) | Yes |
| `MGUN_BLK` | Activates machine gun | Yes |
| `WALLOFF_BLK` | Disables side walls | Yes |
| `REVERSE_BLK` | Reverses paddle controls | Yes |
| `PAD_SHRINK_BLK` | Shrinks paddle | Yes |
| `PAD_EXPAND_BLK` | Expands paddle | Yes |
| `EXTRABALL_BLK` | Adds extra life | Yes |
| `STICKY_BLK` | Activates sticky paddle | Yes |

**Killer ball mode:** Destroys all blocks without bouncing (bypass collision bounce logic). Rendered as frame index 4 (`killer.xpm`).

**`SHOTS_TO_KILL_SPECIAL`** = 3 — special blocks require 3 bullet hits to destroy.

---

## 7. Paddle

### Sizes

| Constant | ID | Width (px) | Half-Width |
|----------|----|-----------|------------|
| `PADDLE_SMALL` | 4 | 40 | 20 |
| `PADDLE_MEDIUM` | 5 | 50 | 25 |
| `PADDLE_HUGE` | 6 | 70 | 35 |

Height: 15 pixels (visible), `PADDLE_HEIGHT` = 9 (collision).

### Movement

| Constant | Value |
|----------|-------|
| `PADDLE_VEL` | 10 px/frame |
| `DIST_BASE` | 30 px from bottom of play area |

Direction constants: `PADDLE_NONE` = 0, `PADDLE_LEFT` = 1, `PADDLE_SHOOT` = 2, `PADDLE_RIGHT` = 3.

### Control Modes

| Mode | Constant | Behavior |
|------|----------|----------|
| Keyboard | `CONTROL_KEYS` (0) | Left/Right arrows or J/L keys |
| Mouse | `CONTROL_MOUSE` (1) | Paddle follows pointer position |

Toggle via `G` key. Default: `CONTROL_KEYS` (or `CONTROL_MOUSE` if `-keys` not specified).

### Special States

- **Reverse mode** (`reverseOn`): Left/right controls inverted. Set by `REVERSE_BLK`. Reset on ball death.
- **Sticky mode** (`stickyOn`): Ball sticks to paddle on contact, launches on shoot key/click. Set by `STICKY_BLK`. Auto-launches after `BALL_AUTO_ACTIVE_DELAY` (3000ms).

### Paddle Size Transitions

- `PAD_SHRINK_BLK`: HUGE -> MEDIUM -> SMALL
- `PAD_EXPAND_BLK`: SMALL -> MEDIUM -> HUGE
- Ball death: resets to HUGE (2x expand calls)
- New game: starts at HUGE

---

## 8. Gun/Bullet System

### Constants

| Constant | Value |
|----------|-------|
| `MAX_BULLETS` | 20 (max ammo) |
| `MAX_MOVING_BULLETS` | 40 (max in flight) |
| `MAX_TINKS` | 40 (max impact effects) |
| `BULLET_WIDTH` | 7 |
| `BULLET_HEIGHT` | 16 |
| `BULLET_WC` | 3 (width center) |
| `BULLET_HC` | 8 (height center) |
| `BULLET_DY` | -7 (upward velocity) |
| `BULLET_START_Y` | `PLAY_HEIGHT - 40` |
| `BULLET_FRAME_RATE` | 3 (update every 3 frames) |
| `TINK_WIDTH` | 10 |
| `TINK_HEIGHT` | 5 |
| `TINK_DELAY` | 100 frames (visible duration) |
| `NUMBER_OF_BULLETS_NEW_LEVEL` | 4 |

### Firing Modes

- **Normal gun:** 1 bullet at `paddlePos`
- **Fast gun** (machine gun): 2 bullets at `paddlePos +/- (paddleSize / 3)`

### Bullet-Block Collision by Type

| Block Type | Bullet Effect |
|-----------|--------------|
| Standard blocks | Destroyed on single hit |
| `COUNTER_BLK` | Decrements counter; destroyed at 0 |
| `HYPERSPACE_BLK` | Immune (bullet absorbed, block redrawn) |
| `BLACK_BLK` | Immune (bullet absorbed, block redrawn) |
| Special blocks* | Require 3 hits (`counterSlide` decremented) |

*Special blocks requiring 3 shots: `REVERSE_BLK`, `MGUN_BLK`, `STICKY_BLK`, `WALLOFF_BLK`, `MULTIBALL_BLK`, `PAD_EXPAND_BLK`, `PAD_SHRINK_BLK`, `DEATH_BLK`.

### Ammo Management

- New level: reset to `NUMBER_OF_BULLETS_NEW_LEVEL` (4)
- Ammo pickup blocks add bullets (capped at `MAX_BULLETS` = 20)
- `MGUN_BLK` activates unlimited mode
- Empty magazine: "click" sound, no shot

### Sounds

| Sound | Volume | Trigger |
|-------|--------|---------|
| "shotgun" | 50 | Firing |
| "shoot" | 80 | Tink impact |
| "ballshot" | 50 | Ball destroyed by bullet |
| "click" | 99 | Empty magazine |

---

## 9. Scoring & Bonuses

### Score Multipliers

- `x2Bonus`: 2x multiplier (lasts 25 blocks, set by `BONUSX2_BLK`)
- `x4Bonus`: 4x multiplier (lasts 15 blocks, set by `BONUSX4_BLK`)
- Applied via `ComputeScore(inc)`: if x2 active, `inc *= 2`; else if x4 active, `inc *= 4`

### Block Point Values

See [Block System](#6-block-system) table. Notable values:
- Standard colors: 100-150
- `ROAMER_BLK`: 400
- `COUNTER_BLK`: 200
- `DROP_BLK`: (18 - row) * 100 (dynamic, 200-1800)
- `BOMB_BLK`, `BULLET_BLK`, `MAXAMMO_BLK`: 50
- `DEATH_BLK`, `BLACK_BLK`: 0

### Extra Lives

| Constant | Value |
|----------|-------|
| `START_LIVES` | 3 |
| `MAX_LIVES` | 6 |
| `NEW_LIVE_SCORE_INC` | 100,000 points |

Extra life awarded every 100,000 points.

### Bonus Sequence States

| State | Description |
|-------|-------------|
| `BONUS_TEXT` | Display bonus screen title |
| `BONUS_SCORE` | Show congratulations |
| `BONUS_BONUS` | Tally collected bonus coins |
| `BONUS_LEVEL` | Level completion bonus |
| `BONUS_BULLET` | Remaining bullet bonus |
| `BONUS_TIME` | Time bonus calculation |
| `BONUS_HSCORE` | High score ranking |
| `BONUS_END_TEXT` | "Prepare for next level" |
| `BONUS_WAIT` | Wait for frame transition |
| `BONUS_FINISH` | Complete sequence, start next level |

### Bonus Score Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `BONUS_COIN_SCORE` | 3,000 | Per bonus coin collected |
| `SUPER_BONUS_SCORE` | 50,000 | If bonus coins > MAX_BONUS (8) |
| `BULLET_SCORE` | 500 | Per unused bullet |
| `LEVEL_SCORE` | 100 | Multiplied by level number |
| `TIME_BONUS` | 100 | Per second remaining |
| `PADDLE_HIT_SCORE` | 10 | Per paddle hit |
| `EYEDUDE_HIT_BONUS` | 10,000 | Eye dude kill bonus |
| `MAX_BONUS` | 8 | Threshold for super bonus |
| `SAVE_LEVEL` | 5 | Auto-save every 5 levels |
| `LINE_DELAY` | 100 | Frames between bonus steps |

### Bonus Calculation

When a level is completed:
1. **Bonus coins:** if `numBonus > MAX_BONUS(8)`, award `SUPER_BONUS_SCORE(50,000)`; else award `numBonus * BONUS_COIN_SCORE(3,000)`
2. **Level bonus:** `LEVEL_SCORE(100) * levelNumber`
3. **Bullet bonus:** `remainingBullets * BULLET_SCORE(500)`
4. **Time bonus:** `secondsRemaining * TIME_BONUS(100)`

### Bonus Sounds

| Sound | Volume | Trigger |
|-------|--------|---------|
| "applause" | 80 | Level complete |
| "Doh1" | 80 | No bonus coins |
| "Doh2" | 80 | No level bonus (timer ran out) |
| "Doh3" | 80 | All bullets used |
| "Doh4" | 80 | Timer ran out |
| "supbons" | 80 | Super bonus awarded |
| "bonus" | 50 | Bonus coin added |
| "key" | 50 | Bullet bonus added |

---

## 10. Special Effects & Power-ups

### Active Specials

| Variable | Effect | Set By | Display Label |
|----------|--------|--------|---------------|
| `reverseOn` | Inverted paddle controls | `REVERSE_BLK` | "Reverse" |
| `stickyBat` | Ball sticks to paddle | `STICKY_BLK` | "Sticky" |
| `saving` | Save/catch ball | Internal | "Save" |
| `fastGun` | Dual bullet firing | `MGUN_BLK` | "FastGun" |
| `noWalls` | Side walls disabled (ball wraps) | `WALLOFF_BLK` | "NoWall" |
| `Killer` | Ball destroys without bouncing | Internal | "Killer" |
| `x2Bonus` | 2x score multiplier | `BONUSX2_BLK` | "x2" |
| `x4Bonus` | 4x score multiplier | `BONUSX4_BLK` | "x4" |

**Display layout** in `specialWindow` (180x35):
- Row 1: Reverse (x=5), Save (x=55), NoWall (x=110), x2 (x=155)
- Row 2: Sticky, FastGun, Killer, x4
- Active: yellow text. Inactive: white text. Font: `copyFont`.
- `FLASH` = 500ms for flashing effects.

**`TurnSpecialsOff()`** called at each level via `SetupStage()`. Deactivates: FastGun, Walls (restores), StickyBat, x2Bonus, x4Bonus, Killer.

### Visual SFX Modes

| Constant | Value | Description |
|----------|-------|-------------|
| `SFX_NONE` | 0 | No effect |
| `SFX_SHAKE` | 1 | Window shake (requires backing store) |
| `SFX_FADE` | 2 | Grid fade-in |
| `SFX_BLIND` | 3 | Venetian blind close |
| `SFX_SHATTER` | 4 | Scattered pixmap reveal |
| `SFX_STATIC` | 5 | Static placeholder |

**Shake effect:** Moves `playWindow` randomly +/-3 pixels every `SHAKE_DELAY` (5) frames.

**Fade effect:** Draws grid of vertical and horizontal lines with 12-pixel spacing in black.

**Blind effect:** Divides `PLAY_WIDTH` into 8 sections, copies from buffer in vertical strips.

**Shatter effect:** 10x10 scatter grid, copies buffer in randomized chunks. Randomization arrays: `xscat[10] = {1,9,3,6,2,4,0,7,5,8}`, `yscat[10] = {2,1,0,8,6,4,9,3,7,5}`.

**SFX availability:** Requires backing store (`DoesBackingStore()` returns `Always`). Toggle via `-nosfx` flag or `S` key.

### Border Glow Animation

- Updates every 40 frames
- Alternates between `reds[i]` and `greens[i]` color arrays (7 levels, index 0-6)
- `noWalls` active: border turns green
- Reset via `ResetBorderGlow()`

### Devil Eyes Animation

- 6 frames: `devilblink[0-5]` (57x16 pixels)
- 26-step sequence: `{0,1,2,3,4,5,5,4,3,2,1,0, 0,0, 0,1,2,3,4,5,5,4,3,2,1,0}`
- Position: bottom-right of play area (`PLAY_WIDTH - 33`, `PLAY_HEIGHT - 13`)

---

## 11. Level System

### Level File Format

**Location:** `$XBOING_LEVELS_DIR/levelNN.data` (NN = 01-80, zero-padded)

**Structure:**
```
Line 1:  Level title (string)
Line 2:  Time limit (integer seconds)
Lines 3-17:  Block grid rows (9 characters each, 15 playable rows)
Lines 18-20:  (empty — bottom 3 rows reserved for paddle area)
```

**Example** (`level01.data`):
```
Genesis
120
.........
.........
rrrrrrrrr
bbbbbbbbb
ggggggggg
ttttttttt
.........
.........
000000000
yyyyyyyyy
ppppppppp
B...B...B
.........
.........
.........

```

### Character-to-Block Mapping

| Char | Block Type | Notes |
|------|-----------|-------|
| `.` | (empty) | Blank space |
| `r` | RED_BLK | |
| `g` | GREEN_BLK | |
| `b` | BLUE_BLK | |
| `t` | TAN_BLK | |
| `p` | PURPLE_BLK | |
| `y` | YELLOW_BLK | |
| `w` | BLACK_BLK | Indestructible wall |
| `0` | COUNTER_BLK (slide 0) | Breakable on first hit |
| `1` | COUNTER_BLK (slide 1) | 1 hit to break |
| `2` | COUNTER_BLK (slide 2) | 2 hits |
| `3` | COUNTER_BLK (slide 3) | 3 hits |
| `4` | COUNTER_BLK (slide 4) | 4 hits |
| `5` | COUNTER_BLK (slide 5) | 5 hits (maximum) |
| `+` | ROAMER_BLK | Mobile block |
| `X` | BOMB_BLK | Explodes, chain reaction |
| `D` | DEATH_BLK | Kills ball (hitPoints=3) |
| `L` | EXTRABALL_BLK | Extra life |
| `M` | MGUN_BLK | Machine gun (hitPoints=3) |
| `W` | WALLOFF_BLK | Disable walls (hitPoints=3) |
| `?` | RANDOM_BLK | Random type on load |
| `d` | DROP_BLK | Falls when hit |
| `T` | TIMER_BLK | +20 seconds |
| `m` | MULTIBALL_BLK | Split ball (hitPoints=3) |
| `s` | STICKY_BLK | Sticky paddle (hitPoints=3) |
| `R` | REVERSE_BLK | Reverse controls (hitPoints=3) |
| `<` | PAD_SHRINK_BLK | Shrink paddle (hitPoints=3) |
| `>` | PAD_EXPAND_BLK | Expand paddle (hitPoints=3) |
| `B` | BULLET_BLK | Ammo pickup |
| `c` | MAXAMMO_BLK | Max ammo pickup |
| `H` | HYPERSPACE_BLK | Teleport ball |

### Level Wrapping

`level % MAX_NUM_LEVELS` where `MAX_NUM_LEVELS` = 80. After level 80, wraps back to level 1.

### Background Rotation

Backgrounds cycle through `BACKGROUND_2` through `BACKGROUND_5` per level, wrapping at 6 back to 2.

### Time System

- Countdown timer displayed in MM:SS format
- Color coding:
  - **Green:** > 60 seconds remaining
  - **Yellow:** 10-60 seconds remaining
  - **Red:** 0-10 seconds remaining
- "buzzer" sound plays when time reaches 0

### Shipped Levels

83 level files: `level01.data` through `level80.data`, plus `demo.data`, `editor.data`, and additional levels up to `level83.data`.

### Bonus Block Spawning

During `MODE_GAME`, random bonus blocks spawn at intervals of `frame + (rand() % 2000)`:

| Cases | Block Type | Condition |
|-------|-----------|-----------|
| 0-7 | BONUS_BLK | Always |
| 8-11 | BONUSX2_BLK | If x2 not already active |
| 12-13 | BONUSX4_BLK | If x4 not already active |
| 14-15 | PAD_SHRINK_BLK | Always |
| 16-17 | PAD_EXPAND_BLK | Always |
| 18 | MULTIBALL_BLK | Always |
| 19 | REVERSE_BLK | Always |
| 20-21 | MGUN_BLK | Always |
| 22 | WALLOFF_BLK | Always |
| 23 | EXTRABALL_BLK | Always |
| 24 | DEATH_BLK | Always |
| 25 | DYNAMITE_BLK | 7 color variants |
| 26 | EyeDude enemy | Always |

---

## 12. Level Editor

### Editor States

| State | Description |
|-------|-------------|
| `EDIT_LEVEL` | Initial level loading/setup |
| `EDIT_NONE` | Normal editing (idle) |
| `EDIT_TEST` | Play test mode |
| `EDIT_WAIT` | Waiting for frame transition |
| `EDIT_FINISH` | Exiting editor |

### Tool Panel

- Width: `EDITOR_TOOL_WIDTH` = 120 px (main window resizes to accommodate)
- Block palette in 2 columns
- Counter blocks (1-5) with slide indices at bottom
- Up to 50 block tool entries

### Grid Constraints

- Editable rows: `MAX_ROW_EDIT` = `MAX_ROW - 3` = 15 (bottom 3 rows reserved for paddle)
- Editable columns: `MAX_COL` = 9
- Grid cells: `PLAY_WIDTH / MAX_COL` x `PLAY_HEIGHT / MAX_ROW`

### Mouse Controls

| Button | Action |
|--------|--------|
| Left (Button1) | Draw selected block (drag supported) |
| Middle (Button2) | Erase block (drag supported) |
| Right (Button3) | Display block hitPoints |

### Key Controls

| Key | Action |
|-----|--------|
| Q, q | Quit editor (prompts if unsaved) |
| R, r | Redraw editor grid |
| L, l | Load a level (1-MAX_NUM_LEVELS) |
| S, s | Save level |
| T, t | Set time limit (1-3600 seconds) |
| N, n | Set level name (max 25 chars) |
| P, p | Start/stop play test mode |
| C, c | Clear entire level (with prompt) |
| H | Flip board horizontally |
| Shift+H | Scroll board horizontally |
| V | Flip board vertically |
| Shift+V | Scroll board vertically |

### Play Test Mode

- Saves editor level to temp file in `/tmp/`
- Initializes with: lives=3, score=0, paddle=HUGE, bullets=4, no time limit
- Supports ball activation and shooting
- Restores original level on exit
- Cleans up temp file with `unlink()`

### Board Manipulation

- `FlipBoardHorizontal()` — mirror left-right
- `ScrollBoardHorizontal()` — rotate left (wrap around)
- `FlipBoardVertical()` — mirror top-bottom
- `ScrollBoardVertical()` — rotate up (wrap around)
- Random blocks converted to `RANDOM_BLK` during operations

### State Transitions

```
EDIT_LEVEL -> EDIT_NONE (setup complete)
EDIT_NONE  -> EDIT_TEST (play test)
EDIT_TEST  -> EDIT_NONE (test finished)
EDIT_NONE  -> EDIT_FINISH (quit)
EDIT_FINISH -> MODE_INTRO
```

---

## 13. High Score System

### Dual Score Files

| Type | Constant | File Path | Locking |
|------|----------|-----------|---------|
| Global | `GLOBAL` (2) | `XBOING_SCORE_FILE` env or `HIGH_SCORE_FILE` define | Yes (LOCK_EX/LOCK_UN) |
| Personal | `PERSONAL` (1) | `~/.xboing-scores` | No |

### File Format

```
[highScoreHeader]           84 bytes
  u_long version            4 bytes (network byte order, SCORE_VERSION=2)
  char masterText[80]       80 bytes ("Boing Master" wisdom text)

[highScoreEntry x 10]      960 bytes (96 bytes each)
  u_long score              4 bytes (network byte order)
  u_long level              4 bytes (network byte order)
  time_t gameTime           4 bytes (network byte order)
  time_t time               4 bytes (network byte order)
  char name[40]             40 bytes
  uid_t userId              4 bytes (network byte order)

Total: 1,044 bytes
```

All numeric fields stored in network byte order (big-endian) via `htonl()`/`ntohl()`.

### Display States

| State | Description |
|-------|-------------|
| `HIGHSCORE_TITLE` | Show title screen (10 frame delay) |
| `HIGHSCORE_SHOW` | Display scores table (blind effect transition) |
| `HIGHSCORE_WAIT` | Wait for frame transition |
| `HIGHSCORE_SPARKLE` | Animate sparkles down score list |
| `HIGHSCORE_FINISH` | Exit to MODE_PREVIEW |

### Score Management

- `NUM_HIGHSCORES` = 10
- Scores sorted in descending order (bubble sort)
- **Global scores:** One entry per user (higher score replaces lower)
- **Personal scores:** Multiple entries allowed
- Rank 1 holder sets "Boing Master" wisdom text

### File Locking

- Uses `lockf()` (`F_LOCK`/`F_ULOCK`) or `flock()` (`LOCK_EX`/`LOCK_UN`) depending on platform
- Only global score file is locked
- Configurable via `USE_FLOCK` or `NO_LOCKING` defines

### Default Table

Initial scores: all entries set to score=0, level=1, gameTime=0, name="To be announced!", masterText="Anyone play this game?"

### Sparkle Animation

- 11-frame star animation per score row
- 30 frames per row, 100 frame pause between rows
- Cycles with 200 frame initial delay and 800 frame delay after full cycle

---

## 14. Save/Load System

### Save File Format

**Game state file:** `~/.xboing-saveinfo` (binary)

```c
typedef struct {
    u_long version;     // SAVE_VERSION = 2
    u_long score;       // Current score
    u_long level;       // Level number
    int    levelTime;   // Time left for level
    time_t gameTime;    // Game completion timestamp
    int    livesLeft;   // Lives remaining
    int    startLevel;  // Starting level number
    int    paddleSize;  // Current paddle size
    int    numBullets;  // Bullets in reserve
} saveGameStruct;
```

**Level data file:** `~/.xboing-savelevel` (text, same format as level files)

### Save/Load Behavior

- Save triggered by `Z` key (when save enabled)
- Load triggered by `X` key
- Auto-save every `SAVE_LEVEL` (5) levels during bonus sequence
- Version check: file must match `SAVE_VERSION` (2) or load is rejected
- Binary serialization via `fread()`/`fwrite()`

---

## 15. UI Screens & Animation Sequences

### Presents Sequence (13 states)

| State | Content | Duration |
|-------|---------|----------|
| `PRESENT_FLAG` | Australian flag pixmap | 800 frames |
| `PRESENT_TEXT1` | "Justin" | 300 frames |
| `PRESENT_TEXT2` | "Kibell" | 500 frames |
| `PRESENT_TEXT3` | "Presents" | 750 frames |
| `PRESENT_TEXT_CLEAR` | Clear screen | 10 frames |
| `PRESENT_LETTERS` | "XBOING" letter-by-letter | 300 frames each (1800 total) |
| `PRESENT_SHINE` | Star sparkle on title | 11 frames x 35 = 385 frames |
| `PRESENT_SPECIAL_TEXT1` | Welcome message (char-by-char) | 30 frames/char |
| `PRESENT_SPECIAL_TEXT2` | "Future of planet Earth..." | 30 frames/char |
| `PRESENT_SPECIAL_TEXT3` | "More instructions..." | 30 frames/char |
| `PRESENT_CLEAR` | Scroll-clear from edges to center | 20 frame intervals |
| `PRESENT_WAIT` | Wait state | Variable |
| `PRESENT_FINISH` | Transition to MODE_INTRO | — |

**Key sounds:** "intro" (40), "stamp" (90), "ping" (70), "key" (60), "whoosh" (70).

### Introduction Sequence (6 states)

| State | Content | Duration |
|-------|---------|----------|
| `INTRO_TITLE` | Title pixmap, initial setup | 10 frames |
| `INTRO_BLOCKS` | Display 21 block types with descriptions | — |
| `INTRO_TEXT` | "Insert coin to start the game" | Until endFrame |
| `INTRO_EXPLODE` | Window shatter effect | — |
| `INTRO_FINISH` | Transition to MODE_INSTRUCT | — |
| `INTRO_WAIT` | Wait state | Variable |

**Star animation:** 11 frames, 15 frame intervals, 500 frame pause, random position (0-474, 0-74).

**End frame:** frame + 3000.

### Instructions Sequence (5 states)

| State | Content | Duration |
|-------|---------|----------|
| `INSTRUCT_TITLE` | "- Instructions -" title | 100 frames |
| `INSTRUCT_TEXT` | Multi-paragraph game instructions | Until endFrame |
| `INSTRUCT_SPARKLE` | Sparkle animation | — |
| `INSTRUCT_FINISH` | Transition to MODE_DEMO | — |
| `INSTRUCT_WAIT` | Wait state | Variable |

**End frame:** frame + 7000. Message: "Save the rainforests".

### Demo Sequence (6 states)

| State | Content | Duration |
|-------|---------|----------|
| `DEMO_TITLE` | Load and display `demo.data` | 10 frames |
| `DEMO_BLOCKS` | Show demo level | — |
| `DEMO_TEXT` | Ball/paddle/block interaction tutorial | frame + 5000 |
| `DEMO_SPARKLE` | Sparkle animation | — |
| `DEMO_FINISH` | Transition to MODE_KEYS | — |
| `DEMO_WAIT` | Wait state | Variable |

### Preview Sequence (4 states)

| State | Content | Duration |
|-------|---------|----------|
| `PREVIEW_LEVEL` | Load random level, display blocks | — |
| `PREVIEW_TEXT` | Level name, "Insert coin..." | 5000 frames |
| `PREVIEW_FINISH` | Transition to MODE_INTRO | — |
| `PREVIEW_WAIT` | Wait state | Variable |

Random level: `(rand() % (MAX_NUM_LEVELS - 1)) + 1`. Background cycles 2-5.

### Keys Display (5 states)

| State | Content | Duration |
|-------|---------|----------|
| `KEYS_TITLE` | "- Game Controls -" title | 100 frames |
| `KEYS_TEXT` | Two-column key listing, mouse diagram | Until endFrame |
| `KEYS_SPARKLE` | Sparkle animation | — |
| `KEYS_FINISH` | Transition to MODE_KEYSEDIT | — |
| `KEYS_WAIT` | Wait state | Variable |

**End frame:** frame + 4000. Message: "Drink driving kills!"

### Keys Editor Display (5 states)

| State | Content | Duration |
|-------|---------|----------|
| `KEYSEDIT_TITLE` | "- Level Editor Controls -" | 100 frames |
| `KEYSEDIT_TEXT` | Editor instructions and key listing | Until endFrame |
| `KEYSEDIT_SPARKLE` | Sparkle animation | — |
| `KEYSEDIT_FINISH` | Transition to MODE_HIGHSCORE | — |
| `KEYSEDIT_WAIT` | Wait state | Variable |

**End frame:** frame + 4000. Message: "Be happy!"

### Screen Transition Timing

```
Presents  (~4000+ frames)  -> Intro
Intro     (~3000 frames)   -> Instructions
Instructions (~7000 frames) -> Demo
Demo      (~5000 frames)   -> Keys
Keys      (~4000 frames)   -> KeysEdit
KeysEdit  (~4000 frames)   -> HighScore
HighScore (variable)       -> Preview
Preview   (~5000 frames)   -> Intro (cycle repeats)
```

### Dialogue System

**Types:**
- `TEXT_ENTRY_ONLY` (1): Alphabetic input only
- `NUMERIC_ENTRY_ONLY` (2): Numeric input only
- `ALL_ENTRY` (3): All characters
- `YES_NO_ENTRY` (4): Single character Y/N

**Dimensions:** `DIALOGUE_WIDTH` = `PLAY_WIDTH / 1.3` (~380), `DIALOGUE_HEIGHT` = 120.

**States:** `DIALOGUE_MAP` -> `DIALOGUE_TEXT` (input) -> `DIALOGUE_UNMAP` (fade) -> `DIALOGUE_FINISHED`.

**Icons:** `DISK_ICON` (1) = floppy disk, `TEXT_ICON` (2) = text icon.

### EyeDude Animated Character

| Constant | Value |
|----------|-------|
| `EYEDUDE_WIDTH` | 32 |
| `EYEDUDE_HEIGHT` | 32 |
| `EYEDUDE_WC` | 16 (center) |
| `EYEDUDE_HC` | 16 (center) |
| `EYEDUDE_FRAME_RATE` | 30 |
| `EYEDUDE_HIT_BONUS` | 10,000 points |

**States:** `EYEDUDE_RESET`, `EYEDUDE_WAIT`, `EYEDUDE_NONE`, `EYEDUDE_DIE`, `EYEDUDE_WALK`, `EYEDUDE_TURN`.

**Walk animation:** 6 frames per direction (left/right), movement +/-5 px/frame. Random direction start (50/50). 30% chance to turn at midpoint.

**Clear path check:** Only spawns if top row (row 0) has no occupied blocks.

**Death:** Erases character, awards 10,000 bonus, plays "supbons" sound. Spawn plays "hithere" (volume 100).

### Message System

- `CLEAR_DELAY` = 2000 frames (auto-clear)
- `FADE_DELAY` = 20 frames
- Centered green text in `messWindow`
- Auto-reverts to level name when clear delay expires

---

## 16. Module Dependency Map

### Initialization Order (from `InitialiseGame` in `init.c`)

1. Parse command-line arguments
2. Open X display
3. Configure display settings (sync, error handlers, signals, random seed)
4. Select color visual
5. Create/configure colormap
6. `SetUpAudioSystem()` — audio (if enabled)
7. Map named colors
8. Create windows — `SetUpStage()`, `SetUpDialogue()`
9. Create graphics contexts (6 GCs)
10. Load fonts (4 fonts)
11. Set window backgrounds
12. `SetUpMessaging()` — message system
13. `SetUpBlocks()` — block grid and pixmaps
14. `SetUpBall()` — ball animation frames
15. `SetUpBullet()` — gun/bullet pixmaps
16. `InitialiseScoreDigits()` — score display
17. `SetUpLevel()` — level info display
18. `SetUpPaddle()` — paddle pixmaps
19. `SetUpDialoguePixmaps()` — dialog icons
20. `SetUpEyeDude()` — eyedude animation
21. `SetUpPresents()` — presents sequence
22. `SetUpKeysControl()` — keys display
23. `SetUpKeyEditControl()` — editor keys display
24. `SetUpInstructions()` — instructions screen
25. `SetUpIntroduction()` — intro screen
26. `SetUpBonus()` — bonus system
27. `SetUpHighScore()` — high scores
28. Create color cycling arrays (reds[7], greens[7])
29. Display initial level info and time
30. Draw specials panel
31. Select input events
32. Map main window
33. Install colormap

### Resource Lifecycle (Setup/Free Pairs)

| Module | Setup Function | Free Function |
|--------|---------------|---------------|
| Audio | `SetUpAudioSystem()` | `FreeAudioSystem()` |
| Stage | `SetUpStage()` | `FreeStage()` |
| Blocks | `SetUpBlocks()` | `FreeBlockPixmaps()` |
| Ball | `SetUpBall()` | `FreeBall()` |
| Gun | `SetUpBullet()` | `FreeBullet()` |
| Score | `InitialiseScoreDigits()` | `FreeScoreDigits()` |
| Level | `SetUpLevel()` | `FreeLevel()` |
| Paddle | `SetUpPaddle()` | `FreePaddle()` |
| Dialogue | `SetUpDialogue()` + `SetUpDialoguePixmaps()` | `FreeDialogue()` |
| EyeDude | `SetUpEyeDude()` | `FreeEyeDude()` |
| Presents | `SetUpPresents()` | `FreePresents()` |
| Keys | `SetUpKeysControl()` | `FreeKeysControl()` |
| KeysEdit | `SetUpKeyEditControl()` | `FreeKeyEditControl()` |
| Instructions | `SetUpInstructions()` | `FreeInstructions()` |
| Introduction | `SetUpIntroduction()` | `FreeIntroduction()` |
| Bonus | `SetUpBonus()` | `FreeBonus()` |
| HighScore | `SetUpHighScore()` | `FreeHighScore()` |
| Messaging | `SetUpMessaging()` | `FreeMessaging()` |
| Editor | `SetUpEditor()` | `FreeEditor()` |
| Fonts | (loaded in init.c) | `ReleaseFonts()` |

### Module Interaction Diagram

```
                          main.c
                     (state machine)
                    /    |    |    \
                   /     |    |     \
              init.c  stage.c |   [UI screens]
             (setup)  (windows)|   presents.c
                              |    intro.c
         ball.c ----+    level.c   inst.c
        (physics)   |   (levels)   demo.c
                    |       |      preview.c
       blocks.c ---+    file.c     keys.c
        (grid)      |   (I/O)     keysedit.c
                    |
       paddle.c    gun.c     score.c    bonus.c
      (control)  (bullets)  (display)  (sequence)
                    |
                special.c    sfx.c     highscore.c
               (power-ups)  (effects)  (scores)
                    |
                eyedude.c   dialogue.c  editor.c
               (character)  (input)    (editor)
                    |
                 mess.c     misc.c     error.c
               (messages)  (drawing)   (errors)
                    |
                 audio.c
               (platform)
```

**Key cross-module calls:**
- `ball.c` calls `blocks.c` (collision), `paddle.c` (position), `gun.c` (bullet-ball), `score.c` (points), `level.c` (dead ball/game over), `sfx.c` (effects), `special.c` (power-up checks)
- `blocks.c` calls `score.c` (points), `special.c` (toggle specials), `sfx.c` (explosions), `ball.c` (multiball split)
- `gun.c` calls `blocks.c` (bullet-block collision), `ball.c` (bullet-ball collision), `eyedude.c` (bullet-eyedude collision)
- `level.c` calls `file.c` (level loading), `bonus.c` (level complete), `highscore.c` (game over), `special.c` (reset specials)
- `main.c` calls all modules via mode dispatch and input handling
- `file.c` calls `blocks.c` (grid population), `level.c` (time/title)

### Shutdown Sequence (`ShutDown` in `init.c`)

1. Free all module resources (reverse of init order)
2. Free graphics contexts
3. Release fonts
4. Free colormap
5. Close X display
6. Print exit message
7. `exit(exitCode)`

---

## Source Files Reference

| File | Header | Role |
|------|--------|------|
| `main.c` | `include/main.h` | State machine, event loop, key bindings |
| `init.c` | `include/init.h` | X11 setup, CLI options, shutdown |
| `ball.c` | `include/ball.h` | Ball physics, collision detection |
| `blocks.c` | `include/blocks.h` | Block types, grid, regions |
| `paddle.c` | `include/paddle.h` | Paddle control |
| `gun.c` | `include/gun.h` | Bullet system |
| `score.c` | `include/score.h` | Score display |
| `bonus.c` | `include/bonus.h` | Bonus sequence |
| `level.c` | `include/level.h` | Level management, lives, time |
| `special.c` | `include/special.h` | Power-ups, specials display |
| `stage.c` | `include/stage.h` | Window hierarchy, backgrounds |
| `highscore.c` | `include/highscore.h` | Score files, display |
| `file.c` | `include/file.h` | Level I/O, save/load |
| `sfx.c` | `include/sfx.h` | Visual effects |
| `audio.c` | `include/audio.h` | Audio API (symlink to driver) |
| `misc.c` | `include/misc.h` | Drawing utilities |
| `editor.c` | `include/editor.h` | Level editor |
| `intro.c` | — | Intro screen |
| `demo.c` | — | Demo mode |
| `presents.c` | — | Presents/credits |
| `inst.c` | — | Instructions |
| `preview.c` | — | Level preview |
| `keys.c` | — | Key display |
| `keysedit.c` | — | Editor keys display |
| `dialogue.c` | `include/dialogue.h` | Modal dialogs |
| `eyedude.c` | `include/eyedude.h` | Animated character |
| `mess.c` | `include/mess.h` | Message display |
| `error.c` | `include/error.h` | Error handling |
| `version.sh` | `include/version.h` | Version generation |
| `Makefile` | — | Build system |
