# Legacy C Testing

Testing strategies for tightly-coupled, globally-stateful C code where
the goal is "prove behavior is preserved across modernization", not
"prove the code is right."

## CMocka (Primary Framework)

- v2.0+ supports TAP 14 output for CI integration
- `cmocka_run_group_tests` for test suites
- `cmocka_unit_test_setup_teardown` for fixture-bound tests
- Mocks via `will_return`, `expect_*`, `check_expected`
- State management via the `*state` parameter
- File naming: `tests/test_<module>.c`

## Characterization Testing

The primary technique. You do **not** test what the code SHOULD do —
you test what it ACTUALLY does. Then refactoring proves the behavior
is preserved.

**Process:**

1. Identify a pure-logic function (no I/O, no globals, no rendering)
2. Call it with representative inputs
3. Record the actual outputs verbatim
4. Write assertions matching those outputs exactly
5. The test is now a behavioral contract

**Good targets:**

- Collision math (point-in-region, line intersection, quadratic)
- Scoring arithmetic (multipliers, thresholds, bonus sequence)
- Level file parsing (character → block-type mapping)
- State machine transitions (mode × event → mode)
- Block hit logic (hit-counter decrement, threshold checks)

**Bad targets:**

- Frame-timing-dependent behavior (varies by hardware)
- Rendering pixel output (varies by display)
- Audio playback (hardware-dependent)
- Random behavior (unless RNG is seedable)

## Making Legacy Code Testable

### Problem: Globals everywhere

```c
extern int mode;
extern int livesLeft;
extern int score;
```

### Solution: Extract into struct, pass by pointer

```c
typedef struct {
    int mode;
    int lives_left;
    int score;
} game_state_t;

void ball_update(game_state_t *state, ball_t *ball);
```

### Problem: Logic mixed with X11/SDL calls

```c
void HandleBallCollision(Display *d, Window w) {
    /* 50 lines of math */
    XDrawLine(d, w, gcxor, ...);
    /* 20 more lines of math */
}
```

### Solution: Extract pure function

```c
collision_result_t compute_ball_collision(ball_t *b, block_grid_t *g);
void render_ball_collision(SDL_Renderer *r, collision_result_t *res);
```

### Problem: Implicit init order

`init.c` sets up globals; `ball.c` reads them. Must call `InitGame()`
before anything works.

### Solution: Explicit dependency injection in tests

```c
static int setup(void **state) {
    game_state_t *gs = calloc(1, sizeof(*gs));
    gs->mode = MODE_GAME;
    gs->lives_left = 3;
    *state = gs;
    return 0;
}
```

## Sanitizer-Driven Development

Run ASan + UBSan on **every** test execution.

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

Expected findings in a 30-year-old C codebase:

- Buffer overflows in `sprintf`/`strcpy`
- Signed integer overflow in score / counter math
- Null deref on error paths
- Use-after-free if cleanup order is wrong
- Stack overflow in fixed-size string buffers

## Fuzz Testing

Targets:

- `ReadNextLevel()` — level file parser
- High score file reader (binary, file-locked)
- Save game deserializer
- Command-line argument parsing

Tools:

- **libFuzzer** (clang built-in): `clang -fsanitize=fuzzer,address`
- **AFL++** for deeper exploration

Harness pattern:

```c
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    /* feed bytes to parser; parser must not crash, leak, or trip
       sanitizer */
    return 0;
}
```

Corpus seeded with valid files from `levels/` and existing save
files.

## Test Layers (Priority Order)

| Layer | Cost | Value | When |
|-------|------|-------|------|
| Sanitizer builds | Low (rebuild) | Highest | First, before any changes |
| Unit tests | Low | High | Before touching each module |
| Integration tests | Medium | Medium | After unit tests exist |
| Replay / golden | High | High | After fixed timestep is in place |
| Fuzz tests | Medium | Medium | After parsers are extracted |

## Timing Caveat

The original game's speed was set by 1993 hardware (Sun, SGI Indigo).
Current code has been tweaked to approximate that feel on modern
hardware, but frame timing is **not** canonical. Do not write
characterization tests for timing-dependent behavior. Stick to pure
hardware-independent logic.

## Reference

- `docs/SPECIFICATION.md` — what each module does
- `CLAUDE.md` — testing strategy, quality gates, sanitizer policy
- Glenford Myers, *The Art of Software Testing* (1979) — still
  load-bearing for this kind of work
