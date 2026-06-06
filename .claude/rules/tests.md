---
paths:
  - "tests/**"
---

# Test Conventions

Read `docs/TESTING.md` for the full 5-layer testing guide.

## Integration Test Pattern

1. Create game context via `game_create(argc, argv)`
2. Set `use_keys = true` if testing keyboard input
3. Transition to target mode: `sdl2_state_transition(ctx->state, SDL2ST_*)`
4. Inject key: `inject_key(ctx, SDL_SCANCODE_X)` (calls `begin_frame` + `process_event`)
5. Call handler: `game_input_global(ctx)` for edge-triggered keys, `sdl2_state_update(ctx->state)` for per-tick
6. Assert observable state change

## Edge-Triggered vs Held Keys

- `just_pressed` handlers → `game_input_global` (once per visual frame)
- `pressed` handlers (paddle direction) → `game_input_update` (per tick)
- Putting `just_pressed` in per-tick code causes multi-fire bugs

## Fixtures

- `setup_attract` — SDL2ST_INTRO
- `setup_game` — SDL2ST_GAME with `use_keys = true`
- `setup_editor` — SDL2ST_EDIT

## SDL Video Drivers

- `SDL_VIDEODRIVER=dummy` — no rendering surface, fast, for logic tests
- `SDL_VIDEODRIVER=offscreen` — real rendering without display, for headless pixel tests **only**
- Offscreen is **not** for visual-fidelity comparison against goldens — use the live X11 + ImageMagick capture pipeline (`make modern-screen`, `make modern-bonus`). See `docs/TESTING.md` Layer 4.
- The one existing offscreen test is ASan-only and DISABLED in ctest (SDL_mixer teardown crash)

## Deep Game State

When a test needs MODE_GAME / MODE_BONUS state without driving
input, use the savegame v2 fixture pattern: build
`savegame_data_t` + `savegame_level_t` in memory, call
`savegame_system_restore(ctx, &info, &lvl)`. For end-of-level
states, an empty grid trips `block_system_still_active==false`
on the next tick. See `tools/gen_bonus_fixtures.c` for the
reference setup and `docs/TESTING.md` for the full pattern.

## Ball Tests

- Use `tick_until_ball_ready(ctx)` + `launch_ball(ctx)` helpers when needing an active ball
- Ball state transitions use `>=` frame checks (not `==`) — defensive against skipped frames

## Registration

- `xboing_add_integration_test(test_name)` in `tests/CMakeLists.txt`
- Environment: `SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy` (default)
- Override with `set_tests_properties` for screenshot tests needing `offscreen`
