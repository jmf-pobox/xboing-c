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

### LLM judge comparison

```bash
make visual-check                               # compare all screens
```

Or targeted:

```bash
.tmp/venv/bin/python scripts/llm_compare.py \
    --original tests/golden/original/intro/TITLE-000.png \
    --modern .tmp/visual-check/modern/intro/TITLE-000.png \
    --screen intro
```

Requires `ANTHROPIC_API_KEY`. Run `make visual-check-setup` once to
install Python dependencies.

### Headless pixel comparison

`tests/test_attract_screenshots.c` captures framebuffers using
`SDL_VIDEODRIVER=offscreen` (real rendering without a display). It
compares direct-entry vs C-key-entry screenshots pixel-by-pixel.

**Important:** `SDL_VIDEODRIVER=dummy` produces no pixels (all black).
Use `offscreen` for any test that needs real rendered output.

This test is disabled in the default ctest suite (crashes in non-ASan
builds due to a pre-existing SDL_mixer teardown bug). Run under ASan:

```bash
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
    ./build-asan/tests/test_attract_screenshots
```

### Capturing overlays (dialogue, pause)

For screens that require user interaction (Q dialogue, pause overlay):

1. Enter the base mode programmatically
2. Inject the key that triggers the overlay
3. Tick a few frames for the overlay to render
4. Capture via `sdl2_renderer_save_screenshot` or `capture_and_save`

Example in `test_attract_screenshots.c::test_capture_dialogue`.

### Adding golden references for new screens

1. Check if the original can reach the screen via the attract cycle
   or `-visual-capture` flag
2. If not (e.g., dialogue), automate with `xdotool` to send keystrokes
   to the running original binary, then capture with `import`
3. Save to `tests/golden/original/<screen>/`
4. Commit the golden files

### User verification

Open images in eog — one at a time (eog does not support side-by-side):

```bash
eog .tmp/original_screenshot.png    # close when done
eog .tmp/modern_screenshot.png      # compare mentally
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
