# Testing XBoing

This document covers the complete testing strategy. Read it in full
before writing any code or tests.

Testing a game is harder than testing a library. Most game code has
tight coupling to global state, hardware, and frame timing. The
strategy is to progressively decouple and test at every layer.

## Layer 1: Unit Tests (CMocka)

Pure logic with no side effects. Highest-value, lowest-cost testing.

### What to test

- Math utilities (collision geometry, trig paddle bounce, velocity clamping)
- State machines (game modes, ball states, block states, bonus sequence)
- Serialization (save/load round-trips, level file parsing, config parsing)
- Game rules (scoring, block point values, extra life thresholds, multipliers)
- String handling and text processing

### How to make legacy code testable

- Extract pure functions from modules that mix logic with I/O
- Introduce seams: pass function pointers instead of calling globals directly
- Replace `#include`-coupled singletons with struct pointers passed as parameters
- One test file per source file: `tests/test_<module>.c`

### How to run

```bash
ctest --test-dir build --output-on-failure        # all tests
ctest --test-dir build -R test_ball_system         # one test
```

## Layer 2: Integration Tests

Multiple subsystems working together, but still deterministic. Uses
`SDL_VIDEODRIVER=dummy` and `SDL_AUDIODRIVER=dummy` for headless
operation in CI.

### What to test

- Game loop ticking with fixed timestep (no real clock)
- Ball-block-paddle interaction lifecycle
- Level loading → block spawning → state verification
- Save → load → verify round-trip
- Keybindings: inject synthetic SDL events, verify state changes

### Keybinding test pattern

The keybinding tests (`tests/test_keybindings.c`) use this pattern:

1. Create game context via `game_create`
2. Transition to the target mode (INTRO, GAME, EDIT)
3. Inject a key via `inject_key(ctx, SDL_SCANCODE_X)` which calls
   `sdl2_input_begin_frame` + `sdl2_input_process_event`
4. Call the handler: `game_input_global(ctx)` for once-per-frame
   keys, `sdl2_state_update(ctx->state)` for per-tick keys
5. Assert observable state change

**Edge-triggered keys** (`just_pressed`) go in `game_input_global`
(called once per visual frame). **Held-key inputs** (paddle direction)
go in `game_input_update` (called per tick). Putting `just_pressed`
handlers in per-tick code causes multi-fire bugs.

### Fixtures

- `setup_attract` — game context in SDL2ST_INTRO
- `setup_game` — game context in SDL2ST_GAME with `use_keys=true`
- `setup_editor` — game context in SDL2ST_EDIT

### How to set up

- Use `SDL_VIDEODRIVER=dummy` (no rendering surface, fast)
- Headless — no display required
- Fixed random seed for reproducibility
- Tests registered via `xboing_add_integration_test()` in
  `tests/CMakeLists.txt`

### Savegame fixture pattern — seeding arbitrary game state

Save/load v2 (`docs/specs/2026-05-28-savegame-v2.md`, ADR-038) is
the load-bearing mechanism for reaching deep game state in tests.

**The problem it solves.** Without a fixture mechanism, a test that
needs to verify behavior at level 25 with KILLER active and a
half-damaged BLACK_BLK on the grid would have to either:

1. Replay 25 levels of synthetic input — slow, brittle, and
   re-runs every time the physics or scoring evolves.
2. Hand-construct the in-memory `game_ctx_t` by calling every
   subsystem's setter — verbose and silently goes stale when a
   subsystem grows new fields.

**The pattern.** The pure
`savegame_system_capture` / `savegame_system_restore` functions
(`include/savegame_system.h`) are the canonical way to seed deep
state.  A test writes a `savegame_data_t` + `savegame_level_t` in
memory, calls `savegame_system_restore(ctx, &info, &lvl)`, and the
context is now in that state — same as if the player had played
there, saved with `Z`, and later loaded with `X`.

```c
savegame_data_t info;
savegame_level_t lvl;
savegame_io_init(&info);
savegame_level_init(&lvl);
info.level = 25;
info.score = 125000;
info.lives_left = 2;
info.balls[0] = (savegame_ball_t){
    .active = 1, .state = BALL_ACTIVE,
    .x = 247, .y = 520, .dx = 3, .dy = -4,
};
info.specials.killer = 1;
lvl.cells[3][4] = (savegame_cell_t){
    .occupied = 1, .block_type = BLACK_BLK,
    .next_frame_offset = 20, /* mid-cooldown */
};
savegame_system_restore(ctx, &info, &lvl);
/* The ctx is now at level 25 with KILLER, a moving ball, and a
 * BLACK_BLK that will exit cooldown in 20 frames.  Tick from here. */
```

**Disk-backed variant.** When the test wants to exercise the JSON
I/O too (or share a fixture between tests), write the same data
via `savegame_io_write` + `savegame_level_write` to a tmp path and
call `savegame_system_load(ctx)` after pointing `XDG_DATA_HOME` at
the tmp dir.  See `tests/test_savegame_system.c::setup` for the
canonical `XDG_DATA_HOME` redirection pattern that preserves the
caller's environment.

**Why this matters for Layer 4.**  The visual fidelity pipeline
captures golden screenshots of specific game states.  Without the
fixture pattern, reaching "level 25 with active KILLER + 3
explosions in progress" requires scripting 25 levels of replay,
and every physics change re-breaks those replays.  With the fixture,
the test writes a JSON file with the desired state, loads it,
captures the screenshot — and the only failure mode the test cares
about is the rendering, not the path to get there.

**Caveat: it doesn't match the original 1996 behavior.** The
original loads `BALL_CREATE → BALL_READY` (ball reset to paddle,
waiting for space).  V2 resumes the ball mid-flight.  This is a
deliberate deviation, documented in ADR-038.  The fixture pattern
is the reason — without mid-flight resume, the loaded state isn't
actually "the saved state" and the fixture leverage evaporates.

## Layer 3: Replay / Golden Tests

Record inputs, replay deterministically, compare results.

### What to test

- Full game sequences (attract → gameplay → victory/defeat)
- Known bug reproduction
- Mode transition sequences (attract cycle order)

### Replay infrastructure

`tests/test_replay.c` provides:

- `replay_init(rctx, ctx, script)` — bind a script to a game context
- `replay_tick(rctx)` — one tick: `begin_frame` → inject scheduled
  events → `game_input_global` → `sdl2_state_update` → advance frame
- `replay_tick_until(rctx, target_frame)` — tick until frame N
- Scripts are arrays of `replay_event_t`:
  `{frame, action, pressed}` terminated by `REPLAY_END`

### How to run

```bash
ctest --test-dir build -R test_replay
ctest --test-dir build -R test_golden_attract
```

## Layer 4: Visual Fidelity Tests

Screenshot capture + comparison for pixel-accuracy against the
original 1996 game. This is the layer that catches rendering bugs
invisible to unit and integration tests.

### When to use

Any change that affects rendering: new screens, visual polish,
sprite changes, font changes, layout adjustments, dialogue overlays.

### Golden reference capture

Requires an X11 display (`DISPLAY` must be set). The capture scripts
use ImageMagick `import` which needs a live X server. Headless CI
uses the offscreen pixel comparison instead (see below).

Capture screenshots of the original binary as reference:

```bash
make golden-screen SCREEN=intro INTERVAL=200    # one screen
make golden-all INTERVAL=200                     # all attract screens
```

This runs `scripts/visual_capture.sh original` which:

1. Launches `original/xboing` with `-visual-capture <mode>`
2. Reads `XBOING_SNAPSHOT mode/substate/seq` signals from stdout
3. Captures the X11 window via ImageMagick `import`
4. Saves PNGs to `tests/golden/original/<mode>/`

Golden references are committed to the repo.

### Modern screenshot capture

```bash
make modern-screen SCREEN=intro INTERVAL=200
```

Saves to `.tmp/visual-check/modern/<mode>/`.

### How the capture pipeline works

`scripts/visual_capture.sh` launches the binary with
`-visual-capture <mode>`, reads `XBOING_SNAPSHOT mode/substate/seq`
lines from stdout via a FIFO, and captures the X11 window via
ImageMagick `import` on every signal. The script terminates on
`XBOING_SNAPSHOT_DONE` OR when the binary exits (FIFO EOF).

**Requires a visible X11 display.** `import -window` needs a real
window — headless runs (`SDL_VIDEODRIVER=offscreen`) won't work.
Use `make capture` targets on a workstation, not in CI.

### Adding a new screen to the capture pipeline

Adding a new screen (e.g., bonus, gameover) requires changes in
**four** places. Missing any one of them causes silent failures.

1. **`src/game_init.c`** — add a name function and `vc_check` branch:

   ```c
   static const char *vc_bonus_name(int s)
   {
       switch (s) {
           case BONUS_STATE_TITLE: return "title";
           case BONUS_STATE_SHOW:  return "show";
           /* Persistent / WAIT states MUST be named — they're how
            * interval-sampling catches frames after fast transitions. */
           case BONUS_STATE_WAIT:  return "wait";
           default: return NULL;
       }
   }
   ```

   Add a pre-state snapshot for the mode's system in `stub_tick`,
   and a branch in `vc_check` that selects the right name function
   based on the SDL mode.

2. **`src/sdl2_cli.c`** — map the screen name to its `SDL2ST_*` mode
   value in the `-visual-capture` argument parser.

3. **`Makefile`** — add the screen to the `visual-check` target's
   screen list:

   ```makefile
   for screen in presents intro instruct demo keys keysedit \
                 preview highscore bonus; do ...
   ```

4. **Capture and commit the golden:**

   ```bash
   make golden-screen SCREEN=bonus INTERVAL=200
   git add tests/golden/original/bonus/
   ```

### State-transition gotchas

Some screens transition through multiple sub-states in a single
tick because of `ATTRACT_FRAME_MULTIPLIER=6` (each game tick runs
6 sub-system updates). The `vc_check` logic catches this via two
paths:

- **Pre/post transition detection** — signals the *pre* state's
  name when the tick changes the state
- **Persistent-state interval sampling** — signals every `INTERVAL`
  frames while a state is held (e.g., WAIT, SPARKLE)

If your screen only produces one screenshot named `title-000.png`
when more states exist, check that the persistent states (typically
WAIT) are named in your `vc_*_name()` function. An unnamed state
returns NULL from the name function and never gets sampled.

### Build dependency gotcha

CMake's Makefile generator sometimes misses dependencies when you
change a `.c` file that's compiled into a static library —
`xboing` won't relink even though the underlying source changed.
Symptoms: code changes in `game_init.c` don't appear in
`./build/xboing`.

**Fix:** reconfigure to refresh dependency tracking:

```bash
cmake --preset debug
cmake --build build --target xboing
```

A `touch src/<file>.c` workaround works but may not invalidate the
linked static library. Reconfiguring is the safe path.

### Capturing original goldens

```bash
make golden-screen SCREEN=intro INTERVAL=200    # one screen
make golden-all INTERVAL=200                     # all attract screens
```

The original binary supports `-visual-capture <mode>` directly.
Goldens land in `tests/golden/original/<mode>/` and are committed.

### Capturing dialogue / pause overlays

These screens require key injection. Use `xdotool` against the
running window:

```bash
./original/xboing &
sleep 2
xdotool search --name "XBoing" key q       # open quit dialogue
sleep 0.3
import -window "$(xdotool search --name 'XBoing' | head -1)" \
    tests/golden/original/dialogue/quit-000.png
pkill -f xboing
```

For programmatic capture in tests, see
`tests/test_attract_screenshots.c::test_capture_dialogue` —
uses `SDL_VIDEODRIVER=offscreen` + `sdl2_renderer_save_screenshot`.

### LLM judge comparison

```bash
make visual-check                               # compare all 8 screens
```

The Makefile target captures any missing modern screenshots first,
then runs `scripts/visual_check.py` which compares each modern
screenshot to its golden via Claude vision API and reports findings
in JSON.

Targeted single-screen comparison:

```bash
.tmp/venv/bin/python scripts/llm_compare.py \
    --original tests/golden/original/preview/level-000.png \
    --modern .tmp/visual-check/modern/preview/wait-002.png \
    --screen preview
```

Requires `ANTHROPIC_API_KEY`. Run `make visual-check-setup` once.

### User verification

Open both images side-by-side via two eog instances:

```bash
eog tests/golden/original/preview/level-000.png &
eog .tmp/visual-check/modern/preview/wait-002.png &
```

Each instance is a separate window — arrange them on screen for
direct comparison.

### Headless pixel comparison

`tests/test_attract_screenshots.c` captures framebuffers using
`SDL_VIDEODRIVER=offscreen` (real rendering without a display).
Use `offscreen`, NOT `dummy` — `dummy` produces black pixels.

This test is disabled in the default ctest suite (crashes in
non-ASan builds due to a pre-existing SDL_mixer teardown bug).
Run under ASan:

```bash
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    ./build-asan/tests/test_attract_screenshots
```

### Definition of Done gates 4-6

Visual fidelity tests map to the Definition of Done:

- **Gate 4 (visual verification):** Build, launch, navigate to the
  affected screen, capture screenshot, compare against golden
- **Gate 5 (LLM judge):** Run `make visual-check` or targeted
  `llm_compare.py`. New major findings must be fixed
- **Gate 6 (user verification):** Open in eog, user confirms

## Layer 5: Fuzz Testing

Feed malformed data to parsers and deserializers.

### What to fuzz

- Level file parsers
- Save file deserializers
- Config file parsers
- Asset loaders

### Tools

libFuzzer (built into clang) or AFL++.

## Testing Priority

1. **Sanitizer builds first** — find memory bugs before any changes
2. **Serialization round-trips** — save/load is highest-risk for data loss
3. **Game rule unit tests** — capture current behavior before touching logic
4. **Input replay infrastructure** — enables regression testing
5. **Visual fidelity** — catches rendering bugs invisible to code tests
6. **Parser fuzz tests** — old C parsers are where security bugs live

## CI Coverage

| Layer | Runs in CI | Runs locally |
|-------|-----------|-------------|
| Unit tests | Yes (`test.yml`) | `make test` |
| Integration tests | Yes (`test.yml`) | `make test` |
| ASan + UBSan | Yes (`test.yml`) | `make asan-test` |
| Replay/golden | Yes (`test.yml`) | `make test` |
| Visual fidelity (LLM) | No | `make visual-check` |
| Visual fidelity (headless pixel) | No (disabled) | ASan build only |
| Fuzz testing | No | Manual |
| Format check | Yes (`lint.yml`) | `make format-check` |
| cppcheck | Yes (`lint.yml`) | `make cppcheck-src` |
| Markdown lint | Yes (`docs.yml`) | `make lint` |
| Debian package | No | `make deb-lint` |
